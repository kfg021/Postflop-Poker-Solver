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
    for (int i = 0; i < chanceNode.numActions; ++i) {
        ActionID actionID = tree.allActions[chanceNode.actionsOffset + i];
        std::size_t nextNodeIndex = tree.allNextNodeIndices[chanceNode.actionsOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());

        if (setContainsCard(availableCards, rules.getCardCorrespondingToChance(actionID))) {
            player0ExpectedValueSum += cfr(rules, playerHands, playerWeights, tree.allNodes[nextNodeIndex], tree);
        }
    }

    return player0ExpectedValueSum / std::popcount(availableCards);
}

float cfrDecision(
    const IGameRules& rules,
    const std::array<CardSet, 2>& playerHands,
    const std::array<float, 2>& playerWeights,
    const DecisionNode& decisionNode,
    Tree& tree
) {
    auto getTrainingDataIndex = [&decisionNode](std::uint16_t trainingDataSet, std::uint8_t actionIndex) -> std::size_t {
        assert(trainingDataSet < decisionNode.numTrainingDataSets);
        return decisionNode.trainingDataOffset + (trainingDataSet * decisionNode.numActions) + actionIndex;
    };

    auto calculateCurrentStrategy = [&decisionNode, &tree, &getTrainingDataIndex](std::uint16_t trainingDataSet) -> FixedVector<float, MaxNumActions> {
        float totalPositiveRegret = 0.0f;
        for (int i = 0; i < decisionNode.numActions; ++i) {
            float regretSum = tree.allRegretSums[getTrainingDataIndex(trainingDataSet, i)];
            if (regretSum > 0.0f) {
                totalPositiveRegret += regretSum;
            }
        }

        assert(decisionNode.numActions > 0);
        FixedVector<float, MaxNumActions> currentStrategy(decisionNode.numActions, 1.0f / decisionNode.numActions);
        if (totalPositiveRegret > 0.0f) {
            for (int i = 0; i < decisionNode.numActions; ++i) {
                float regretSum = tree.allRegretSums[getTrainingDataIndex(trainingDataSet, i)];
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

    float currentPlayerExpectedValue = 0.0f;
    FixedVector<float, MaxNumActions> currentPlayerActionUtility(decisionNode.numActions, 0.0f);
    for (int i = 0; i < decisionNode.numActions; ++i) {
        std::array<float, 2> newPlayerWeights = playerWeights;
        newPlayerWeights[getPlayerID(decisionNode.player)] *= currentPlayerStrategy[i];

        std::size_t nextNodeIndex = tree.allNextNodeIndices[decisionNode.actionsOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());

        float player0ExpectedValue = cfr(rules, playerHands, newPlayerWeights, tree.allNodes[nextNodeIndex], tree);
        currentPlayerActionUtility[i] = (decisionNode.player == Player::P0) ? player0ExpectedValue : -player0ExpectedValue; // Zero-sum game
        currentPlayerExpectedValue += currentPlayerActionUtility[i] * currentPlayerStrategy[i];
    }

    for (int i = 0; i < decisionNode.numActions; ++i) {
        float regret = currentPlayerActionUtility[i] - currentPlayerExpectedValue;
        std::size_t trainingIndex = getTrainingDataIndex(trainingDataSet, i);
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
        Player winner = rules.getShowdownWinner(playerHands, board);
        return (winner == Player::P0) ? showdownNode.reward : -showdownNode.reward; // Zero-sum game
    };

    CardSet availibleCardsForRunout = rules.getDeck() & ~(playerHands[0] | playerHands[1] | showdownNode.board);

    switch (showdownNode.street) {
        case Street::River: {
            // All cards dealt - no runout needed
            return static_cast<float>(getPlayer0Reward(showdownNode.board));
        }

        case Street::Turn: {
            // One runout card neeed
            std::int32_t player0ExpectedValueSum = 0;
            for (CardID riverCard = 0; riverCard < StandardDeckSize; ++riverCard) {
                if (setContainsCard(availibleCardsForRunout, riverCard)) {
                    CardSet boardAfterRiver = showdownNode.board | cardIDToSet(riverCard);
                    player0ExpectedValueSum += getPlayer0Reward(boardAfterRiver);
                }
            }

            int numPossibleRunouts = std::popcount(availibleCardsForRunout);
            return static_cast<float>(player0ExpectedValueSum) / numPossibleRunouts;
        }

        case Street::Flop: {
            // Two runout cards needed
            std::int32_t player0ExpectedValueSum = 0;
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

            int numPossibleTurnCards = std::popcount(availibleCardsForRunout);
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