#include "solver/cfr.hpp"

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "solver/node.hpp"
#include "solver/tree.hpp"
#include "util/fixed_vector.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace {
CardSet getAvailableCards(const IGameRules& rules, PlayerArray<std::uint16_t> handIndices, CardSet board) {
    CardSet player0Hand = rules.mapIndexToHand(Player::P0, handIndices[Player::P0]);
    CardSet player1Hand = rules.mapIndexToHand(Player::P1, handIndices[Player::P1]);
    return rules.getDeck() & ~(player0Hand | player1Hand | board);
}

float cfrChance(
    const IGameRules& rules,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const ChanceNode& chanceNode,
    Tree& tree
) {
    CardSet availableCardsForChance = getAvailableCards(rules, handIndices, chanceNode.board);

    float player0ExpectedValueSum = 0.0f;
    for (int i = 0; i < chanceNode.chanceDataSize; ++i) {
        CardID nextCard = tree.allChanceCards[chanceNode.chanceDataOffset + i];
        std::size_t nextNodeIndex = tree.allChanceNextNodeIndices[chanceNode.chanceDataOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());

        if (setContainsCard(availableCardsForChance, nextCard)) {
            player0ExpectedValueSum += cfr(rules, handIndices, weights, tree.allNodes[nextNodeIndex], tree);
        }
    }

    return player0ExpectedValueSum / getSetSize(availableCardsForChance);
}

float cfrDecision(
    const IGameRules& rules,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const DecisionNode& decisionNode,
    Tree& tree
) {
    auto calculateCurrentStrategy = [&decisionNode, &tree](std::uint16_t trainingDataSet) -> FixedVector<float, MaxNumActions> {
        std::uint8_t numActions = decisionNode.decisionDataSize;

        float totalRegret = 0.0f;
        for (int i = 0; i < numActions; ++i) {
            float regretSum = tree.allRegretSums[getTrainingDataIndex(decisionNode, trainingDataSet, i)];
            assert(regretSum >= 0.0f);
            totalRegret += regretSum;
        }

        assert(numActions > 0);
        FixedVector<float, MaxNumActions> currentStrategy(numActions, 1.0f / numActions);
        if (totalRegret > 0.0f) {
            for (int i = 0; i < numActions; ++i) {
                float regretSum = tree.allRegretSums[getTrainingDataIndex(decisionNode, trainingDataSet, i)];
                assert(regretSum >= 0.0f);
                currentStrategy[i] = regretSum / totalRegret;
            }
        }

        return currentStrategy;
    };

    std::uint16_t trainingDataSet = handIndices[decisionNode.player];
    FixedVector<float, MaxNumActions> currentPlayerStrategy = calculateCurrentStrategy(trainingDataSet);
    std::uint8_t numActions = decisionNode.decisionDataSize;

    float currentPlayerExpectedValue = 0.0f;
    FixedVector<float, MaxNumActions> currentPlayerActionUtility(numActions, 0.0f);
    for (int i = 0; i < numActions; ++i) {
        PlayerArray<float> newWeights = weights;
        newWeights[decisionNode.player] *= currentPlayerStrategy[i];

        std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());

        float player0ExpectedValue = cfr(rules, handIndices, newWeights, tree.allNodes[nextNodeIndex], tree);
        currentPlayerActionUtility[i] = (decisionNode.player == Player::P0) ? player0ExpectedValue : -player0ExpectedValue; // Zero-sum game
        currentPlayerExpectedValue += currentPlayerActionUtility[i] * currentPlayerStrategy[i];
    }

    for (int i = 0; i < numActions; ++i) {
        float regret = currentPlayerActionUtility[i] - currentPlayerExpectedValue;
        std::size_t trainingIndex = getTrainingDataIndex(decisionNode, trainingDataSet, i);
        tree.allRegretSums[trainingIndex] += weights[getOpposingPlayer(decisionNode.player)] * regret;
        tree.allStrategySums[trainingIndex] += weights[decisionNode.player] * currentPlayerStrategy[i];

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
    PlayerArray<std::uint16_t> handIndices,
    const ShowdownNode& showdownNode
) {
    auto getPlayer0Reward = [&rules, &handIndices, &showdownNode](CardSet board) -> int {
        CardSet player0Hand = rules.mapIndexToHand(Player::P0, handIndices[Player::P0]);
        CardSet player1Hand = rules.mapIndexToHand(Player::P1, handIndices[Player::P1]);
        switch (rules.getShowdownResult(player0Hand, player1Hand, board)) {
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

    CardSet availableCardsForRunout = getAvailableCards(rules, handIndices, showdownNode.board);

    switch (showdownNode.street) {
        case Street::River: {
            // All cards dealt - no runout needed
            return static_cast<float>(getPlayer0Reward(showdownNode.board));
        }

        case Street::Turn: {
            // One runout card neeed
            int player0ExpectedValueSum = 0;
            for (CardID riverCard = 0; riverCard < StandardDeckSize; ++riverCard) {
                if (setContainsCard(availableCardsForRunout, riverCard)) {
                    CardSet boardAfterRiver = showdownNode.board | cardIDToSet(riverCard);
                    player0ExpectedValueSum += getPlayer0Reward(boardAfterRiver);
                }
            }

            int numPossibleRunouts = getSetSize(availableCardsForRunout);
            return static_cast<float>(player0ExpectedValueSum) / numPossibleRunouts;
        }

        case Street::Flop: {
            // Two runout cards needed
            int player0ExpectedValueSum = 0;
            for (CardID turnCard = 0; turnCard < StandardDeckSize; ++turnCard) {
                for (CardID riverCard = turnCard + 1; riverCard < StandardDeckSize; ++riverCard) {
                    // Deal order doesn't matter for showdowns, so we can assume that turnCard < riverCard
                    bool turnCardAvailable = setContainsCard(availableCardsForRunout, turnCard);
                    bool riverCardAvailable = setContainsCard(availableCardsForRunout, riverCard);
                    if (turnCardAvailable && riverCardAvailable) {
                        CardSet boardAfterTurnRiver = showdownNode.board | cardIDToSet(turnCard) | cardIDToSet(riverCard);
                        player0ExpectedValueSum += getPlayer0Reward(boardAfterTurnRiver);
                    }
                }
            }

            int numPossibleTurnCards = getSetSize(availableCardsForRunout);
            int numPossibleRunouts = (numPossibleTurnCards * (numPossibleTurnCards - 1)) / 2;
            return static_cast<float>(player0ExpectedValueSum) / numPossibleRunouts;
        }

        default:
            assert(false);
            return 0.0f;
    }
}
} // namespace

float cfr(
    const IGameRules& rules,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
) {
    assert(tree.isTreeSkeletonBuilt() && tree.isFullTreeBuilt());

    switch (node.getNodeType()) {
        case NodeType::Chance:
            return cfrChance(
                rules,
                handIndices,
                weights,
                node.chanceNode,
                tree
            );
        case NodeType::Decision:
            return cfrDecision(
                rules,
                handIndices,
                weights,
                node.decisionNode,
                tree
            );
        case NodeType::Fold:
            return cfrFold(node.foldNode);
        case NodeType::Showdown:
            return cfrShowdown(
                rules,
                handIndices,
                node.showdownNode
            );
        default:
            assert(false);
            return 0.0f;
    }
}