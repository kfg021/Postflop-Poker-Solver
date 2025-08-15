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

namespace {
enum class TraversalMode : std::uint8_t {
    CfrUpdateP0,
    CfrUpdateP1,
    ExpectedValue,
};

CardSet getAvailableCards(const IGameRules& rules, PlayerArray<std::uint16_t> handIndices, CardSet board) {
    CardSet player0Hand = rules.mapIndexToHand(Player::P0, handIndices[Player::P0]);
    CardSet player1Hand = rules.mapIndexToHand(Player::P1, handIndices[Player::P1]);
    return rules.getDeck() & ~(player0Hand | player1Hand | board);
}

float traverseTree(
    const IGameRules& rules,
    TraversalMode mode,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
);

float traverseChance(
    const IGameRules& rules,
    TraversalMode mode,
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
            player0ExpectedValueSum += traverseTree(rules, mode, handIndices, weights, tree.allNodes[nextNodeIndex], tree);
        }
    }

    return player0ExpectedValueSum / getSetSize(availableCardsForChance);
}

float cfrPlusDecision(
    const IGameRules& rules,
    TraversalMode mode,
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

    assert((mode == TraversalMode::CfrUpdateP0) || (mode == TraversalMode::CfrUpdateP1));

    std::uint16_t trainingDataSet = handIndices[decisionNode.player];
    FixedVector<float, MaxNumActions> playerCurrentStrategy = calculateCurrentStrategy(trainingDataSet);
    std::uint8_t numActions = decisionNode.decisionDataSize;

    // Calculate a player's expected value by playing all actions weighted by the strategy
    float currentPlayerExpectedValue = 0.0f;
    FixedVector<float, MaxNumActions> currentPlayerActionUtility(numActions, 0.0f);
    for (int i = 0; i < numActions; ++i) {
        PlayerArray<float> newWeights = weights;
        newWeights[decisionNode.player] *= playerCurrentStrategy[i];

        std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());

        float player0ExpectedValue = traverseTree(rules, mode, handIndices, newWeights, tree.allNodes[nextNodeIndex], tree);
        currentPlayerActionUtility[i] = (decisionNode.player == Player::P0) ? player0ExpectedValue : -player0ExpectedValue; // Zero-sum game
        currentPlayerExpectedValue += currentPlayerActionUtility[i] * playerCurrentStrategy[i];
    }

    // In CFR+, we only update regrets for the currently traversing player
    bool shouldUpdateRegrets =
        ((mode == TraversalMode::CfrUpdateP0) && (decisionNode.player == Player::P0)) |
        ((mode == TraversalMode::CfrUpdateP1) && (decisionNode.player == Player::P1));

    if (shouldUpdateRegrets) {
        for (int i = 0; i < numActions; ++i) {
            float regret = currentPlayerActionUtility[i] - currentPlayerExpectedValue;
            std::size_t trainingIndex = getTrainingDataIndex(decisionNode, trainingDataSet, i);
            tree.allRegretSums[trainingIndex] += weights[getOpposingPlayer(decisionNode.player)] * regret;
            tree.allStrategySums[trainingIndex] += weights[decisionNode.player] * playerCurrentStrategy[i];

            // In CFR+, we erase negative regrets for faster convergence
            tree.allRegretSums[trainingIndex] = std::max(tree.allRegretSums[trainingIndex], 0.0f);
        }
    }

    return (decisionNode.player == Player::P0) ? currentPlayerExpectedValue : -currentPlayerExpectedValue; // Return EV from player 0's perspective
}

float expectedValueDecision(
    const IGameRules& rules,
    TraversalMode mode,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const DecisionNode& decisionNode,
    Tree& tree
) {
    assert(mode == TraversalMode::ExpectedValue);

    std::uint16_t trainingDataSet = handIndices[decisionNode.player];
    FixedVector<float, MaxNumActions> playerAverageStrategy = getAverageStrategy(decisionNode, tree, trainingDataSet);
    std::uint8_t numActions = decisionNode.decisionDataSize;

    // Calculate a player's expected value by playing all actions weighted by the strategy
    float currentPlayerExpectedValue = 0.0f;
    for (int i = 0; i < numActions; ++i) {
        std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());

        float player0ExpectedValue = traverseTree(rules, mode, handIndices, weights, tree.allNodes[nextNodeIndex], tree);
        float currentPlayerActionUtility = (decisionNode.player == Player::P0) ? player0ExpectedValue : -player0ExpectedValue; // Zero-sum game
        currentPlayerExpectedValue += currentPlayerActionUtility * playerAverageStrategy[i];
    }

    return (decisionNode.player == Player::P0) ? currentPlayerExpectedValue : -currentPlayerExpectedValue; // Return EV from player 0's perspective
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
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
) {
    assert(tree.isTreeSkeletonBuilt() && tree.isFullTreeBuilt());

    switch (node.getNodeType()) {
        case NodeType::Chance:
            return traverseChance(rules, mode, handIndices, weights, node.chanceNode, tree);
        case NodeType::Decision:
            switch (mode) {
                case TraversalMode::CfrUpdateP0:
                case TraversalMode::CfrUpdateP1:
                    return cfrPlusDecision(rules, mode, handIndices, weights, node.decisionNode, tree);
                case TraversalMode::ExpectedValue:
                    return expectedValueDecision(rules, mode, handIndices, weights, node.decisionNode, tree);
                default:
                    assert(false);
                    return 0.0f;
            }
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
    TraversalMode mode = (traverser == Player::P0) ? TraversalMode::CfrUpdateP0 : TraversalMode::CfrUpdateP1;
    static_cast<void>(traverseTree(rules, mode, handIndices, weights, node, tree));
}

float calculatePlayer0ExpectedValue(
    const IGameRules& rules,
    PlayerArray<std::uint16_t> handIndices,
    const Node& node,
    Tree& tree
) {
    // The weights only matter for the CFR update step, don't need them for EV calculation
    static constexpr PlayerArray<float> IgnoredWeights = { 0.0f, 0.0f };
    return traverseTree(rules, TraversalMode::ExpectedValue, handIndices, IgnoredWeights, node, tree);
}