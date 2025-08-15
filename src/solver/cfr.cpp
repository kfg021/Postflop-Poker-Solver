#include "solver/cfr.hpp"

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "solver/node.hpp"
#include "solver/tree.hpp"
#include "util/fixed_vector.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {
enum class TraversalMode : std::uint8_t {
    CfrUpdate,
    ExpectedValue,
    BestResponse
};

CardSet getAvailableCards(const IGameRules& rules, PlayerArray<std::uint16_t> handIndices, CardSet board) {
    CardSet player0Hand = rules.mapIndexToHand(Player::P0, handIndices[Player::P0]);
    CardSet player1Hand = rules.mapIndexToHand(Player::P1, handIndices[Player::P1]);
    return rules.getDeck() & ~(player0Hand | player1Hand | board);
}

float traverseTree(
    const IGameRules& rules,
    TraversalMode mode,
    Player traverser,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
);

float traverseChance(
    const IGameRules& rules,
    TraversalMode mode,
    Player traverser,
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
            player0ExpectedValueSum += traverseTree(rules, mode, traverser, handIndices, weights, tree.allNodes[nextNodeIndex], tree);
        }
    }

    return player0ExpectedValueSum / getSetSize(availableCardsForChance);
}

float traverseDecision(
    const IGameRules& rules,
    TraversalMode mode,
    Player traverser,
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

    FixedVector<float, MaxNumActions> playerStrategy;
    if (mode == TraversalMode::CfrUpdate) {
        playerStrategy = calculateCurrentStrategy(trainingDataSet);
    }
    else {
        playerStrategy = getAverageStrategy(decisionNode, tree, trainingDataSet);
    }

    std::uint8_t numActions = decisionNode.decisionDataSize;

    if ((mode == TraversalMode::BestResponse) && (traverser == decisionNode.player)) {
        // Calcluate a player's best response by calculating the EV of each possible action and choosing the best one
        static constexpr float NegativeInfinity = -std::numeric_limits<float>::infinity();
        float currentPlayerBestResponse = NegativeInfinity;

        for (int i = 0; i < numActions; ++i) {
            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + i];
            assert(nextNodeIndex < tree.allNodes.size());

            float player0ExpectedValue = traverseTree(rules, mode, traverser, handIndices, weights, tree.allNodes[nextNodeIndex], tree);
            float currentPlayerExpectedValue = (decisionNode.player == Player::P0) ? player0ExpectedValue : -player0ExpectedValue; // Zero-sum game
            currentPlayerBestResponse = std::max(currentPlayerBestResponse, currentPlayerExpectedValue);
        }

        assert(currentPlayerBestResponse != NegativeInfinity);
        return (decisionNode.player == Player::P0) ? currentPlayerBestResponse : -currentPlayerBestResponse;
    }
    else {
        // Calculate a player's expected value by playing all actions weighted by the strategy
        float currentPlayerExpectedValue = 0.0f;
        FixedVector<float, MaxNumActions> currentPlayerActionUtility(numActions, 0.0f);

        for (int i = 0; i < numActions; ++i) {
            PlayerArray<float> newWeights = weights;
            newWeights[decisionNode.player] *= playerStrategy[i];

            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + i];
            assert(nextNodeIndex < tree.allNodes.size());

            float player0ExpectedValue = traverseTree(rules, mode, traverser, handIndices, newWeights, tree.allNodes[nextNodeIndex], tree);
            currentPlayerActionUtility[i] = (decisionNode.player == Player::P0) ? player0ExpectedValue : -player0ExpectedValue; // Zero-sum game
            currentPlayerExpectedValue += currentPlayerActionUtility[i] * playerStrategy[i];
        }

        if ((mode == TraversalMode::CfrUpdate) && (traverser == decisionNode.player)) {
            // In CFR+, we only update regrets for the currently traversing player
            for (int i = 0; i < numActions; ++i) {
                float regret = currentPlayerActionUtility[i] - currentPlayerExpectedValue;
                std::size_t trainingIndex = getTrainingDataIndex(decisionNode, trainingDataSet, i);
                tree.allRegretSums[trainingIndex] += weights[getOpposingPlayer(decisionNode.player)] * regret;
                tree.allStrategySums[trainingIndex] += weights[decisionNode.player] * playerStrategy[i];

                // In CFR+, we erase negative regrets for faster convergence
                tree.allRegretSums[trainingIndex] = std::max(tree.allRegretSums[trainingIndex], 0.0f);
            }
        }

        return (decisionNode.player == Player::P0) ? currentPlayerExpectedValue : -currentPlayerExpectedValue; // Return EV from player 0's perspective
    }
}

float traverseFold(const FoldNode& foldNode) {
    Player winner = foldNode.remainingPlayer;
    float reward = static_cast<float>(foldNode.remainingPlayerReward);
    return (winner == Player::P0) ? reward : -reward;
}

float traverseShowdown(
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

float traverseTree(
    const IGameRules& rules,
    TraversalMode mode,
    Player traverser,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
) {
    assert(tree.isTreeSkeletonBuilt() && tree.isFullTreeBuilt());

    switch (node.getNodeType()) {
        case NodeType::Chance:
            return traverseChance(rules, mode, traverser, handIndices, weights, node.chanceNode, tree);
        case NodeType::Decision:
            return traverseDecision(rules, mode, traverser, handIndices, weights, node.decisionNode, tree);
        case NodeType::Fold:
            return traverseFold(node.foldNode);
        case NodeType::Showdown:
            return traverseShowdown(rules, handIndices, node.showdownNode);
        default:
            assert(false);
            return 0.0f;
    }
}
} // namespace

void cfrPlus(
    const IGameRules& rules,
    Player traverser,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
) {
    static_cast<void>(traverseTree(rules, TraversalMode::CfrUpdate, traverser, handIndices, weights, node, tree));
}

float calculatePlayerExpectedValue(
    const IGameRules& rules,
    Player player,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
) {
    float player0ExpectedValue = traverseTree(rules, TraversalMode::ExpectedValue, Player::P0, handIndices, weights, node, tree);
    return (player == Player::P0) ? player0ExpectedValue : -player0ExpectedValue;
}

float calculatePlayerBestResponse(
    const IGameRules& rules,
    Player player,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
) {
    return traverseTree(rules, TraversalMode::BestResponse, player, handIndices, weights, node, tree);
}