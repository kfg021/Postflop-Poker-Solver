#include "solver/cfr.hpp"

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "solver/node.hpp"
#include "solver/tree.hpp"
#include "util/fixed_vector.hpp"

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>

float cfrChance(
    const IGameRules& rules,
    const std::array<CardSet, 2>& playerHands,
    const std::array<float, 2>& playerWeights,
    const ChanceNode& chanceNode,
    Tree& tree
) {
    CardSet availableCards = rules.getDeck() & ~(playerHands[0] | playerHands[1] | chanceNode.board);

    float player0ExpectedValueSum = 0.0f;
    for (int i = 0; i < chanceNode.chanceDataSize; ++i) {
        CardID nextCard = tree.allChanceCards[chanceNode.chanceDataOffset + i];
        std::size_t nextNodeIndex = tree.allChanceNextNodeIndices[chanceNode.chanceDataOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());

        if (setContainsCard(availableCards, nextCard)) {
            player0ExpectedValueSum += cfr(rules, playerHands, playerWeights, tree.allNodes[nextNodeIndex], tree);
        }
    }

    return player0ExpectedValueSum / getSetSize(availableCards);
}

float cfrDecision(
    const IGameRules& rules,
    const std::array<CardSet, 2>& playerHands,
    const std::array<float, 2>& playerWeights,
    const DecisionNode& decisionNode,
    Tree& tree
) {
    auto calculateCurrentStrategy = [&decisionNode, &tree](std::uint16_t trainingDataSet) -> FixedVector<float, MaxNumActions> {
        std::uint8_t numActions = decisionNode.decisionDataSize;

        float totalPositiveRegret = 0.0f;
        for (int i = 0; i < numActions; ++i) {
            float regretSum = tree.allRegretSums[getTrainingDataIndex(decisionNode, trainingDataSet, i)];
            if (regretSum > 0.0f) {
                totalPositiveRegret += regretSum;
            }
        }

        assert(numActions > 0);
        FixedVector<float, MaxNumActions> currentStrategy(numActions, 1.0f / numActions);
        if (totalPositiveRegret > 0.0f) {
            for (int i = 0; i < numActions; ++i) {
                float regretSum = tree.allRegretSums[getTrainingDataIndex(decisionNode, trainingDataSet, i)];
                if (regretSum > 0.0f) {
                    currentStrategy[i] = regretSum / totalPositiveRegret;
                }
                else {
                    currentStrategy[i] = 0.0f;
                }
            }
        }

        return currentStrategy;
    };

    const CardSet& currentPlayerHand = playerHands[getPlayerID(decisionNode.player)];
    std::uint16_t trainingDataSet = rules.mapHandToIndex(decisionNode.player, currentPlayerHand);
    FixedVector<float, MaxNumActions> currentPlayerStrategy = calculateCurrentStrategy(trainingDataSet);
    std::uint8_t numActions = decisionNode.decisionDataSize;

    float currentPlayerExpectedValue = 0.0f;
    FixedVector<float, MaxNumActions> currentPlayerActionUtility(numActions, 0.0f);
    for (int i = 0; i < numActions; ++i) {
        std::array<float, 2> newPlayerWeights = playerWeights;
        newPlayerWeights[getPlayerID(decisionNode.player)] *= currentPlayerStrategy[i];

        std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());

        float player0ExpectedValue = cfr(rules, playerHands, newPlayerWeights, tree.allNodes[nextNodeIndex], tree);
        currentPlayerActionUtility[i] = (decisionNode.player == Player::P0) ? player0ExpectedValue : -player0ExpectedValue; // Zero-sum game
        currentPlayerExpectedValue += currentPlayerActionUtility[i] * currentPlayerStrategy[i];
    }

    for (int i = 0; i < numActions; ++i) {
        float regret = currentPlayerActionUtility[i] - currentPlayerExpectedValue;
        std::size_t trainingIndex = getTrainingDataIndex(decisionNode, trainingDataSet, i);
        tree.allRegretSums[trainingIndex] += playerWeights[getOpposingPlayerID(decisionNode.player)] * regret;
        tree.allStrategySums[trainingIndex] += playerWeights[getPlayerID(decisionNode.player)] * currentPlayerStrategy[i];

        if (tree.allRegretSums[trainingIndex] < 0.0f) {
            tree.allRegretSums[trainingIndex] = 0.0f;
        }
    }

    return (decisionNode.player == Player::P0) ? currentPlayerExpectedValue : -currentPlayerExpectedValue; // Return EV from player 0's perspective
}

float cfrFold(const FoldNode& foldNode) {
    Player winner = foldNode.remainingPlayer;
    float reward = static_cast<float>(foldNode.remainingPlayerReward);
    return (winner == Player::P0) ? reward : -reward;
}

float cfrShowdown(
    const IGameRules& rules,
    const std::array<CardSet, 2>& playerHands,
    const ShowdownNode& showdownNode
) {
    auto getPlayer0Reward = [&rules, &playerHands, &showdownNode](CardSet board) -> std::int32_t {
        switch (rules.getShowdownResult(playerHands, board)) {
            case ShowdownResult::P0Win:
                return showdownNode.reward;
            case ShowdownResult::P1Win:
                return -showdownNode.reward;
            case ShowdownResult::Tie:
                return 0;
            default:
                assert(false);
                return 0;
        }
    };

    CardSet availibleCardsForRunout = rules.getDeck() & ~(playerHands[0] | playerHands[1] | showdownNode.board);

    switch (showdownNode.street) {
        case Street::River: {
            // All cards dealt - no runout needed
            return static_cast<float>(getPlayer0Reward(showdownNode.board));
        }

        case Street::Turn: {
            // One runout card neeed
            int player0ExpectedValueSum = 0;
            for (CardID riverCard = 0; riverCard < StandardDeckSize; ++riverCard) {
                if (setContainsCard(availibleCardsForRunout, riverCard)) {
                    CardSet boardAfterRiver = showdownNode.board | cardIDToSet(riverCard);
                    player0ExpectedValueSum += getPlayer0Reward(boardAfterRiver);
                }
            }

            int numPossibleRunouts = getSetSize(availibleCardsForRunout);
            return static_cast<float>(player0ExpectedValueSum) / numPossibleRunouts;
        }

        case Street::Flop: {
            // Two runout cards needed
            int player0ExpectedValueSum = 0;
            for (CardID turnCard = 0; turnCard < StandardDeckSize; ++turnCard) {
                for (CardID riverCard = 0; riverCard < StandardDeckSize; ++riverCard) {
                    if (turnCard == riverCard) continue;

                    bool turnCardAvailible = setContainsCard(availibleCardsForRunout, turnCard);
                    bool riverCardAvailible = setContainsCard(availibleCardsForRunout, riverCard);
                    if (turnCardAvailible && riverCardAvailible) {
                        CardSet boardAfterTurnRiver = showdownNode.board | cardIDToSet(turnCard) | cardIDToSet(riverCard);
                        player0ExpectedValueSum += getPlayer0Reward(boardAfterTurnRiver);
                    }
                }
            }

            int numPossibleTurnCards = getSetSize(availibleCardsForRunout);
            int numPossibleRunouts = (numPossibleTurnCards * (numPossibleTurnCards - 1)) / 2;
            return static_cast<float>(player0ExpectedValueSum) / numPossibleRunouts;
        }

        default:
            assert(false);
            return 0.0f;
    }
}

float cfr(
    const IGameRules& rules,
    const std::array<CardSet, 2>& playerHands,
    const std::array<float, 2>& playerWeights,
    const Node& node,
    Tree& tree
) {
    assert(tree.isTreeSkeletonBuilt() && tree.isFullTreeBuilt());

    switch (node.nodeType) {
        case NodeType::Chance:
            return cfrChance(
                rules,
                playerHands,
                playerWeights,
                node.chanceNode,
                tree
            );
        case NodeType::Decision:
            return cfrDecision(
                rules,
                playerHands,
                playerWeights,
                node.decisionNode,
                tree
            );
        case NodeType::Fold:
            return cfrFold(node.foldNode);
        case NodeType::Showdown:
            return cfrShowdown(
                rules,
                playerHands,
                node.showdownNode
            );
        default:
            assert(false);
            return 0.0f;
    }
}