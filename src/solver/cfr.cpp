#include "solver/cfr.hpp"

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "solver/node.hpp"
#include "solver/tree.hpp"
#include "util/fixed_vector.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {
enum class TraversalMode : std::uint8_t {
    VanillaCfr,
    CfrPlus,
    DiscountedCfr,
    ExpectedValue
};

struct TraversalData {
    DiscountParams params;
    PlayerArray<float> weights;
    TraversalMode mode;
    Player traverser;
};

CardSet getAvailableCards(const IGameRules& rules, PlayerArray<std::uint16_t> handIndices, CardSet board) {
    CardSet player0Hand = rules.mapIndexToHand(Player::P0, handIndices[Player::P0]);
    CardSet player1Hand = rules.mapIndexToHand(Player::P1, handIndices[Player::P1]);
    return rules.getDeck() & ~(player0Hand | player1Hand | board);
}

float traverseTree(
    const IGameRules& rules,
    const TraversalData& data,
    PlayerArray<std::uint16_t> handIndices,
    const Node& node,
    Tree& tree
);

float traverseChance(
    const IGameRules& rules,
    const TraversalData& data,
    PlayerArray<std::uint16_t> handIndices,
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
            player0ExpectedValueSum += traverseTree(rules, data, handIndices, tree.allNodes[nextNodeIndex], tree);
        }
    }

    return player0ExpectedValueSum / getSetSize(availableCardsForChance);
}

float cfrDecision(
    const IGameRules& rules,
    const TraversalData& data,
    PlayerArray<std::uint16_t> handIndices,
    const DecisionNode& decisionNode,
    Tree& tree
) {
    auto calculateCurrentStrategy = [&decisionNode, &tree](std::uint16_t trainingDataSet) -> FixedVector<float, MaxNumActions> {
        std::uint8_t numActions = decisionNode.decisionDataSize;
        assert(numActions > 0);

        float totalPositiveRegret = 0.0f;
        for (int i = 0; i < numActions; ++i) {
            float regretSum = tree.allRegretSums[getTrainingDataIndex(decisionNode, trainingDataSet, i)];
            if (regretSum > 0.0f) {
                totalPositiveRegret += regretSum;
            }
        }

        if (totalPositiveRegret == 0.0f) {
            FixedVector<float, MaxNumActions> uniformStrategy(numActions, 1.0f / numActions);
            return uniformStrategy;
        }

        FixedVector<float, MaxNumActions> currentStrategy(numActions, 0.0f);
        for (int i = 0; i < numActions; ++i) {
            float regretSum = tree.allRegretSums[getTrainingDataIndex(decisionNode, trainingDataSet, i)];
            if (regretSum > 0.0f) {
                currentStrategy[i] = regretSum / totalPositiveRegret;
            }
        }
        return currentStrategy;
    };

    assert(data.mode != TraversalMode::ExpectedValue);

    std::uint16_t trainingDataSet = handIndices[decisionNode.player];
    FixedVector<float, MaxNumActions> playerCurrentStrategy = calculateCurrentStrategy(trainingDataSet);
    std::uint8_t numActions = decisionNode.decisionDataSize;

    // Calculate a player's expected value by playing all actions weighted by the strategy
    float currentPlayerExpectedValue = 0.0f;
    FixedVector<float, MaxNumActions> currentPlayerActionUtility(numActions, 0.0f);
    for (int i = 0; i < numActions; ++i) {
        TraversalData newData = data;
        newData.weights[decisionNode.player] *= playerCurrentStrategy[i];

        std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());

        float player0ExpectedValue = traverseTree(rules, newData, handIndices, tree.allNodes[nextNodeIndex], tree);
        currentPlayerActionUtility[i] = (decisionNode.player == Player::P0) ? player0ExpectedValue : -player0ExpectedValue; // Zero-sum game
        currentPlayerExpectedValue += currentPlayerActionUtility[i] * playerCurrentStrategy[i];
    }

    if (data.traverser == decisionNode.player) {
        for (int i = 0; i < numActions; ++i) {
            float regret = currentPlayerActionUtility[i] - currentPlayerExpectedValue;
            std::size_t trainingIndex = getTrainingDataIndex(decisionNode, trainingDataSet, i);

            if (data.mode == TraversalMode::DiscountedCfr) {
                if (tree.allRegretSums[trainingIndex] > 0.0f) {
                    tree.allRegretSums[trainingIndex] *= data.params.alphaT;
                }
                else {
                    tree.allRegretSums[trainingIndex] *= data.params.betaT;
                }

                tree.allStrategySums[trainingIndex] *= data.params.gammaT;
            }

            tree.allRegretSums[trainingIndex] += data.weights[getOpposingPlayer(decisionNode.player)] * regret;
            tree.allStrategySums[trainingIndex] += data.weights[decisionNode.player] * playerCurrentStrategy[i];

            if (data.mode == TraversalMode::CfrPlus) {
                // In CFR+, we erase negative regrets for faster convergence
                if (tree.allRegretSums[trainingIndex] < 0.0f) {
                    tree.allRegretSums[trainingIndex] = 0.0f;
                }
            }
        }
    }

    return (decisionNode.player == Player::P0) ? currentPlayerExpectedValue : -currentPlayerExpectedValue; // Return EV from player 0's perspective
}

float expectedValueDecision(
    const IGameRules& rules,
    const TraversalData& data,
    PlayerArray<std::uint16_t> handIndices,
    const DecisionNode& decisionNode,
    Tree& tree
) {
    assert(data.mode == TraversalMode::ExpectedValue);

    std::uint16_t trainingDataSet = handIndices[decisionNode.player];
    FixedVector<float, MaxNumActions> playerAverageStrategy = getAverageStrategy(decisionNode, tree, trainingDataSet);
    std::uint8_t numActions = decisionNode.decisionDataSize;

    // Calculate a player's expected value by playing all actions weighted by the strategy
    float currentPlayerExpectedValue = 0.0f;
    for (int i = 0; i < numActions; ++i) {
        std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());

        float player0ExpectedValue = traverseTree(rules, data, handIndices, tree.allNodes[nextNodeIndex], tree);
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
    float reward = static_cast<float>(showdownNode.reward);

    switch (rules.getShowdownResult(handIndices, showdownNode.board)) {
        case ShowdownResult::P0Win:
            return reward;
        case ShowdownResult::P1Win:
            return -reward;
        case ShowdownResult::Tie:
            return 0.0f;
        default:
            assert(false);
            return 0.0f;
    }
}

float traverseTree(
    const IGameRules& rules,
    const TraversalData& data,
    PlayerArray<std::uint16_t> handIndices,
    const Node& node,
    Tree& tree
) {
    assert(tree.isTreeSkeletonBuilt() && tree.isFullTreeBuilt());

    switch (node.getNodeType()) {
        case NodeType::Chance:
            return traverseChance(rules, data, handIndices, node.chanceNode, tree);
        case NodeType::Decision:
            if (data.mode != TraversalMode::ExpectedValue) {
                return cfrDecision(rules, data, handIndices, node.decisionNode, tree);
            }
            else {
                return expectedValueDecision(rules, data, handIndices, node.decisionNode, tree);
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

DiscountParams getDiscountParams(float alpha, float beta, float gamma, int iteration) {
    // TODO: Does t start at 0 or 1?
    float t = static_cast<float>(iteration + 1);
    float a = std::pow(t, alpha);
    float b = std::pow(t, beta);

    return {
        .alphaT = a / (1 + a),
        .betaT = b / (1 + b),
        .gammaT = std::pow(t / (t + 1), gamma)
    };
}

void vanillaCfr(
    const IGameRules& rules,
    Player traverser,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
) {
    // Vanilla CFR doesn't need discount params
    TraversalData data = {
        .weights = weights,
        .mode = TraversalMode::VanillaCfr,
        .traverser = traverser,
    };
    static_cast<void>(traverseTree(rules, data, handIndices, node, tree));
}

void cfrPlus(
    const IGameRules& rules,
    Player traverser,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
) {
    // CFR+ doesn't need discount params
    TraversalData data = {
        .weights = weights,
        .mode = TraversalMode::CfrPlus,
        .traverser = traverser,
    };
    static_cast<void>(traverseTree(rules, data, handIndices, node, tree));
}

void discountedCfr(
    const IGameRules& rules,
    const DiscountParams& params,
    Player traverser,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
) {
    TraversalData data = {
        .params = params,
        .weights = weights,
        .mode = TraversalMode::DiscountedCfr,
        .traverser = traverser
    };
    static_cast<void>(traverseTree(rules, data, handIndices, node, tree));
}

float calculatePlayer0ExpectedValue(
    const IGameRules& rules,
    PlayerArray<std::uint16_t> handIndices,
    const Node& node,
    Tree& tree
) {
    // Expected value calculation doesn't need discount params, weights, or traverser
    TraversalData data = {
        .mode = TraversalMode::ExpectedValue
    };
    return traverseTree(rules, data, handIndices, node, tree);
}