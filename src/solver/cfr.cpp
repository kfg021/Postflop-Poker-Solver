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
#include <span>

namespace {
enum class TraversalMode : std::uint8_t {
    VanillaCfr,
    CfrPlus,
    DiscountedCfr,
    ExpectedValue
};

// struct TraversalData {
//     DiscountParams params;
//     PlayerArray<float> weights;
//     TraversalMode mode;
//     Player traverser;
// };

struct TraversalConstants {
    IGameRules* rules;
    Player traverser;
    TraversalMode mode;
    DiscountParams params;
    PlayerArray<std::vector<CardSet>> rangeHands;
};

PlayerArray<std::span<float>> getInputWeightSpans(std::size_t nodeIndex, const TraversalConstants& constants, Tree& tree) {
    PlayerArray<std::span<float>> inputWeightSpans;

    for (Player player : { Player::P0, Player::P1 }) {
        std::size_t playerRangeSize = constants.rangeHands[player].size();
        inputWeightSpans[player] = {
            tree.allInputWeights[player].begin() + (nodeIndex * playerRangeSize),
            playerRangeSize
        };
    }

    return inputWeightSpans;
}

std::span<float> getOutputExpectedValueSpan(std::size_t nodeIndex, const TraversalConstants& constants, Tree& tree) {
    std::size_t traverserRangeSize = constants.rangeHands[constants.traverser].size();
    return {
        tree.allOutputExpectedValues.begin() + (nodeIndex * traverserRangeSize),
        traverserRangeSize
    };
}

std::span<const float> getStategySumsSpan(const DecisionNode& decisionNode, int trainingDataSet, const Tree& tree) {
    assert(trainingDataSet < decisionNode.numTrainingDataSets);
    return {
        tree.allStrategySums.begin() + decisionNode.trainingDataOffset + (trainingDataSet * decisionNode.decisionDataSize),
        decisionNode.decisionDataSize
    };
}

std::span<float> getStategySumsSpan(const DecisionNode& decisionNode, int trainingDataSet, Tree& tree) {
    assert(trainingDataSet < decisionNode.numTrainingDataSets);
    return {
        tree.allStrategySums.begin() + decisionNode.trainingDataOffset + (trainingDataSet * decisionNode.decisionDataSize),
        decisionNode.decisionDataSize
    };
}

std::span<float> getRegretSumsSpan(const DecisionNode& decisionNode, int trainingDataSet, Tree& tree) {
    assert(trainingDataSet < decisionNode.numTrainingDataSets);
    return {
        tree.allRegretSums.begin() + decisionNode.trainingDataOffset + (trainingDataSet * decisionNode.decisionDataSize),
        decisionNode.decisionDataSize
    };
}

void traverseTree(
    const Node& node,
    const TraversalConstants& constants,
    const PlayerArray<std::span<float>>& rangeWeights,
    Tree& tree,
    std::span<float>& outputExpectedValues
);

void traverseChance(
    const ChanceNode& chanceNode,
    const TraversalConstants& constants,
    const PlayerArray<std::span<float>>& rangeWeights,
    Tree& tree,
    std::span<float> outputExpectedValues
) {
    // TODO: This function assumes that chance cards are equally likely to be dealt.
    // In reality, this is NOT true.
    // The ranges of both players affect the probability that a specific card is dealt.

    std::span<const CardID> nextCards = {
        tree.allChanceCards.begin() + chanceNode.chanceDataOffset,
        chanceNode.chanceDataSize
    };

    std::span<const std::size_t> nextNodeIndices = {
        tree.allChanceNextNodeIndices.begin() + chanceNode.chanceDataOffset,
        chanceNode.chanceDataSize
    };

    std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);
    for (int chanceCardIndex = 0; chanceCardIndex < chanceNode.chanceDataSize; ++chanceCardIndex) {
        const CardID& nextCard = nextCards[chanceCardIndex];
        const std::size_t& nextNodeIndex = nextNodeIndices[chanceCardIndex];
        assert(nextNodeIndices[chanceCardIndex] < tree.allNodes.size());

        PlayerArray<std::span<float>> newRangeWeights = getInputWeightSpans(nextNodeIndex, constants, tree);
        for (Player player : { Player::P0, Player::P1 }) {
            for (int rangeIndex = 0; rangeIndex < newRangeWeights[player].size(); ++rangeIndex) {
                const CardSet& playerHand = constants.rangeHands[player][rangeIndex];
                if (setContainsCard(playerHand, nextCard)) {
                    // The chance card we are about to deal conflicts with a hand in the player's range
                    newRangeWeights[player][rangeIndex] = 0.0f;
                }
                else {
                    newRangeWeights[player][rangeIndex] = rangeWeights[player][rangeIndex];
                    if (player != constants.traverser) {
                        int numChanceCards = StandardDeckSize - getSetSize(chanceNode.board);
                        newRangeWeights[player][rangeIndex] /= numChanceCards;
                    }
                }
            }
        }

        std::span<float> expectedValueAfterCard = getOutputExpectedValueSpan(nextNodeIndex, constants, tree);
        traverseTree(tree.allNodes[nextNodeIndex], constants, newRangeWeights, tree, expectedValueAfterCard);

        assert(expectedValueAfterCard.size() == outputExpectedValues.size());
        for (int rangeIndex = 0; rangeIndex < expectedValueAfterCard.size(); ++rangeIndex) {
            outputExpectedValues[rangeIndex] += expectedValueAfterCard[rangeIndex];
        }
    }
}

void traverseDecision(
    const DecisionNode& decisionNode,
    const TraversalConstants& constants,
    const PlayerArray<std::span<float>>& rangeWeights,
    Tree& tree,
    std::span<float> outputExpectedValues
) {
    auto calculateCurrentStrategy = [&decisionNode, &tree](int trainingDataSet) -> FixedVector<float, MaxNumActions> {
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

    std::span<const ActionID> decisions = {
        tree.allDecisions.begin() + decisionNode.decisionDataOffset,
        decisionNode.decisionDataSize
    };

    std::span<const std::size_t> nextNodeIndices = {
        tree.allChanceNextNodeIndices.begin() + decisionNode.decisionDataOffset,
        decisionNode.decisionDataSize
    };

    std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);
    for (int rangeIndex = 0; rangeIndex < rangeWeights[constants.traverser].size(); ++rangeIndex) {
        FixedVector<float, MaxNumActions> trCurrentStrategy = calculateCurrentStrategy(rangeIndex);
        std::uint8_t numActions = decisionNode.decisionDataSize;

        // Calculate the utility for each hand in the traverser's range by playing all actions weighted by the strategy
        FixedVector<float, MaxNumActions> rangeElementActionUtility(numActions, 0.0f);
        for (int actionIndex = 0; actionIndex < numActions; ++actionIndex) {
            const std::size_t& nextNodeIndex = nextNodeIndices[actionIndex];
            assert(nextNodeIndex < tree.allNodes.size());

            PlayerArray<std::span<float>> newRangeWeights = getInputWeightSpans(nextNodeIndex, constants, tree);
            for (float& weight : newRangeWeights[decisionNode.player]) {
                weight = rangeWeights[decisionNode.player][actionIndex] * playerCurrentStrategy[actionIndex];
            }

            std::span<float> expectedValueAfterAction = getOutputExpectedValueSpan(nextNodeIndex, constants, tree);
            traverseTree(tree.allNodes[nextNodeIndex], constants, newRangeWeights, tree, expectedValueAfterAction);
        }

        // if()

    }

    // std::uint16_t trainingDataSet = handIndices[decisionNode.player];
    // FixedVector<float, MaxNumActions> playerCurrentStrategy = calculateCurrentStrategy(trainingDataSet);
    // std::uint8_t numActions = decisionNode.decisionDataSize;

    // // Calculate a player's expected value by playing all actions weighted by the strategy
    // float currentPlayerExpectedValue = 0.0f;
    // FixedVector<float, MaxNumActions> currentPlayerActionUtility(numActions, 0.0f);
    // for (int i = 0; i < numActions; ++i) {
    //     TraversalData newData = data;
    //     newData.weights[decisionNode.player] *= playerCurrentStrategy[i];

    //     std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + i];
    //     assert(nextNodeIndex < tree.allNodes.size());

    //     float player0ExpectedValue = traverseTree(rules, newData, handIndices, tree.allNodes[nextNodeIndex], tree);
    //     currentPlayerActionUtility[i] = (decisionNode.player == Player::P0) ? player0ExpectedValue : -player0ExpectedValue; // Zero-sum game
    //     currentPlayerExpectedValue += currentPlayerActionUtility[i] * playerCurrentStrategy[i];
    // }

    // if (data.traverser == decisionNode.player) {
    //     for (int i = 0; i < numActions; ++i) {
    //         float regret = currentPlayerActionUtility[i] - currentPlayerExpectedValue;
    //         std::size_t trainingIndex = getTrainingDataIndex(decisionNode, trainingDataSet, i);

    //         if (data.mode == TraversalMode::DiscountedCfr) {
    //             if (tree.allRegretSums[trainingIndex] > 0.0f) {
    //                 tree.allRegretSums[trainingIndex] *= data.params.alphaT;
    //             }
    //             else {
    //                 tree.allRegretSums[trainingIndex] *= data.params.betaT;
    //             }

    //             tree.allStrategySums[trainingIndex] *= data.params.gammaT;
    //         }

    //         tree.allRegretSums[trainingIndex] += data.weights[getOpposingPlayer(decisionNode.player)] * regret;
    //         tree.allStrategySums[trainingIndex] += data.weights[decisionNode.player] * playerCurrentStrategy[i];

    //         if (data.mode == TraversalMode::CfrPlus) {
    //             // In CFR+, we erase negative regrets for faster convergence
    //             if (tree.allRegretSums[trainingIndex] < 0.0f) {
    //                 tree.allRegretSums[trainingIndex] = 0.0f;
    //             }
    //         }
    //     }
    // }

    // return (decisionNode.player == Player::P0) ? currentPlayerExpectedValue : -currentPlayerExpectedValue; // Return EV from player 0's perspective
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

float traverseFold(const FoldNode& foldNode, const TraversalConstants& constants, std::span<float> outputExpectedValues) {
    Player winner = foldNode.remainingPlayer;
    float reward = static_cast<float>(foldNode.remainingPlayerReward);

    // The player who did not fold wins the reward, regardless of their hand or the board
    for (float& expectedValue : outputExpectedValues) {
        expectedValue = (winner == constants.traverser) ? reward : -reward;
    }
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

void traverseTree(
    const Node& node,
    const TraversalConstants& constants,
    const PlayerArray<std::span<float>>& rangeWeights,
    Tree& tree,
    std::span<float>& outputExpectedValues
) {
    assert(tree.isTreeSkeletonBuilt() && tree.isFullTreeBuilt());

    switch (node.getNodeType()) {
        case NodeType::Chance:
            traverseChance(node.chanceNode, constants, rangeWeights, tree, outputExpectedValues);
            break;
        case NodeType::Decision:
            traverseDecision(node.chanceNode, constants, rangeWeights, tree, outputExpectedValues);
            break;
        case NodeType::Fold:
            traverseFold(node.foldNode, constants, outputExpectedValues);
            break;
        case NodeType::Showdown:
            traverseShowdown(node.showdownNode, constants, rangeWeights, outputExpectedValues);
            break;
        default:
            assert(false);
            break;
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
//     TraversalData data = {
//         .mode = TraversalMode::ExpectedValue
//     };
//     return traverseTree(rules, data, handIndices, node, tree);
}

FixedVector<float, MaxNumActions> getAverageStrategy(const DecisionNode& decisionNode, int trainingDataSet, const Tree& tree) {
    std::uint8_t numActions = decisionNode.decisionDataSize;
    assert(numActions > 0);

    std::span<const float> strategySums = getStategySumsSpan(decisionNode, trainingDataSet, tree);

    float total = 0.0f;
    for (float strategySum : strategySums) {
        total += strategySum;
    }

    if (total == 0.0f) {
        FixedVector<float, MaxNumActions> uniformStrategy(numActions, 1.0f / numActions);
        return uniformStrategy;
    }

    FixedVector<float, MaxNumActions> averageStrategy(numActions, 0.0f);
    for (int i = 0; i < numActions; ++i) {
        averageStrategy[i] = strategySums[i] / total;
    }

    return averageStrategy;
}