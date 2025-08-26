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

// TODO: Do arithmetic with doubles, then store result in floats

namespace {
enum class TraversalMode : std::uint8_t {
    VanillaCfr,
    CfrPlus,
    DiscountedCfr,
    ExpectedValue
};

struct TraversalConstants {
    Player traverser;
    TraversalMode mode;
    DiscountParams params;
};

PlayerArray<std::span<float>> getInputWeightSpans(std::size_t nodeIndex, const IGameRules& rules, Tree& tree) {
    PlayerArray<std::span<float>> inputWeightSpans;

    for (Player player : { Player::P0, Player::P1 }) {
        std::size_t playerRangeSize = rules.getRangeHands(player).size();
        inputWeightSpans[player] = {
            tree.allInputWeights[player].begin() + (nodeIndex * playerRangeSize),
            playerRangeSize
        };
    }

    return inputWeightSpans;
}

std::span<float> getOutputExpectedValueSpan(std::size_t nodeIndex, const TraversalConstants& constants, const IGameRules& rules, Tree& tree) {
    std::size_t traverserRangeSize = rules.getRangeHands(constants.traverser).size();
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
    const IGameRules& rules,
    const PlayerArray<std::span<float>>& rangeWeights,
    Tree& tree,
    std::span<float> outputExpectedValues
);

void traverseChance(
    const ChanceNode& chanceNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::span<float>>& rangeWeights,
    Tree& tree,
    std::span<float> outputExpectedValues
) {
    // TODO: This function assumes that chance cards are equally likely to be dealt.
    // In reality, this is NOT true.
    // The ranges of both players affect the probability that a specific card is dealt

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
        CardID nextCard = nextCards[chanceCardIndex];
        std::size_t nextNodeIndex = nextNodeIndices[chanceCardIndex];
        assert(nextNodeIndices[chanceCardIndex] < tree.allNodes.size());

        PlayerArray<std::span<float>> newRangeWeights = getInputWeightSpans(nextNodeIndex, rules, tree);
        for (Player player : { Player::P0, Player::P1 }) {
            for (int rangeIndex = 0; rangeIndex < newRangeWeights[player].size(); ++rangeIndex) {
                CardSet playerHand = rules.getRangeHands(player)[rangeIndex];
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

        std::span<float> expectedValueAfterCard = getOutputExpectedValueSpan(nextNodeIndex, constants, rules, tree);
        traverseTree(
            tree.allNodes[nextNodeIndex],
            constants,
            rules,
            newRangeWeights,
            tree,
            expectedValueAfterCard
        );

        assert(expectedValueAfterCard.size() == outputExpectedValues.size());
        for (int rangeIndex = 0; rangeIndex < expectedValueAfterCard.size(); ++rangeIndex) {
            outputExpectedValues[rangeIndex] += expectedValueAfterCard[rangeIndex];
        }
    }
}

void traverseDecision(
    const DecisionNode& decisionNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::span<float>>& rangeWeights,
    Tree& tree,
    std::span<float> outputExpectedValues
) {
    auto calculateCurrentStrategy = [&decisionNode, &tree](int trainingDataSet) -> FixedVector<float, MaxNumActions> {
        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        std::span<float> regretSums = getRegretSumsSpan(decisionNode, trainingDataSet, tree);

        float totalPositiveRegret = 0.0f;
        for (float regretSum : regretSums) {
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
            if (regretSums[i] > 0.0f) {
                currentStrategy[i] = regretSums[i] / totalPositiveRegret;
            }
        }
        return currentStrategy;
    };

    Player playerToAct = decisionNode.player;
    Player traverser = constants.traverser;

    int playerToActRangeSize = rangeWeights[playerToAct].size();
    int traverserRangeSize = rangeWeights[traverser].size();

    int numActions = static_cast<int>(decisionNode.decisionDataSize);

    bool isCfr = (constants.mode != TraversalMode::ExpectedValue);

    std::span<const std::size_t> nextNodeIndices = {
        tree.allDecisionNextNodeIndices.begin() + decisionNode.decisionDataOffset,
        static_cast<std::size_t>(numActions)
    };

    // TODO: Avoid heap allocation
    std::vector<FixedVector<float, MaxNumActions>> playerToActStrategies(playerToActRangeSize);
    for (int i = 0; i < playerToActRangeSize; ++i) {
        playerToActStrategies[i] = (isCfr ? calculateCurrentStrategy(i) : getAverageStrategy(decisionNode, i, tree));
    }

    // Fill new weight buffers
    for (int actionIndex = 0; actionIndex < numActions; ++actionIndex) {
        std::size_t nextNodeIndex = nextNodeIndices[actionIndex];
        assert(nextNodeIndex < tree.allNodes.size());

        // Copy old weights into new buffer
        PlayerArray<std::span<float>> newRangeWeights = getInputWeightSpans(nextNodeIndex, rules, tree);
        for (Player player : { Player::P0, Player::P1 }) {
            assert(newRangeWeights[player].size() == rangeWeights[player].size());
            for (int i = 0; i < newRangeWeights[player].size(); ++i) {
                newRangeWeights[player][i] = rangeWeights[player][i];
            }
        }

        // Update new weights based on strategies
        for (int rangeIndex = 0; rangeIndex < playerToActRangeSize; ++rangeIndex) {
            newRangeWeights[playerToAct][rangeIndex] *= playerToActStrategies[rangeIndex][actionIndex];
        }
    }

    // Clear output buffer before using
    assert(outputExpectedValues.size() == traverserRangeSize);
    std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

    // Calculate expected values for each hand in the traverser's range
    for (int actionIndex = 0; actionIndex < numActions; ++actionIndex) {
        std::size_t nextNodeIndex = nextNodeIndices[actionIndex];

        // The new range weights have already been copied into this buffer 
        PlayerArray<std::span<float>> newRangeWeights = getInputWeightSpans(nextNodeIndex, rules, tree);

        std::span<float> expectedValuesAfterAction = getOutputExpectedValueSpan(nextNodeIndex, constants, rules, tree);
        traverseTree(
            tree.allNodes[nextNodeIndex],
            constants,
            rules,
            newRangeWeights,
            tree,
            expectedValuesAfterAction
        );

        for (int rangeIndex = 0; rangeIndex < traverserRangeSize; ++rangeIndex) {
            if (traverser == playerToAct) {
                // Traverser is the player to act - weight expected value by strategy
                outputExpectedValues[rangeIndex] += expectedValuesAfterAction[rangeIndex] * playerToActStrategies[rangeIndex][actionIndex];
            }
            else {
                // Traverser is not the player to act - no weighting needed
                outputExpectedValues[rangeIndex] += expectedValuesAfterAction[rangeIndex];
            }
        }
    }

    if (isCfr && (traverser == playerToAct)) {
        float opponentWeightSum = 0.0f;
        for (float weight : rangeWeights[getOpposingPlayer(playerToAct)]) {
            opponentWeightSum += weight;
        }

        for (int rangeIndex = 0; rangeIndex < traverserRangeSize; ++rangeIndex) {
            auto strategySums = getStategySumsSpan(decisionNode, rangeIndex, tree);
            auto regretSums = getRegretSumsSpan(decisionNode, rangeIndex, tree);

            for (int actionIndex = 0; actionIndex < numActions; ++actionIndex) {
                std::size_t nextNodeIndex = nextNodeIndices[actionIndex];

                // The expected values have already been copied into this buffer 
                auto expectedValuesAfterAction = getOutputExpectedValueSpan(nextNodeIndex, constants, rules, tree);

                float regret = expectedValuesAfterAction[rangeIndex] - outputExpectedValues[rangeIndex];

                if (constants.mode == TraversalMode::DiscountedCfr) {
                    if (regretSums[actionIndex] > 0.0f) {
                        regretSums[actionIndex] *= constants.params.alphaT;
                    }
                    else {
                        regretSums[actionIndex] *= constants.params.betaT;
                    }

                    strategySums[actionIndex] *= constants.params.gammaT;
                }

                regretSums[actionIndex] += opponentWeightSum * regret;
                strategySums[actionIndex] += rangeWeights[playerToAct][rangeIndex] * playerToActStrategies[rangeIndex][actionIndex];

                if (constants.mode == TraversalMode::CfrPlus) {
                    // In CFR+, we erase negative regrets for faster convergence
                    if (regretSums[actionIndex] < 0.0f) {
                        regretSums[actionIndex] = 0.0f;
                    }
                }
            }
        }
    }
}

void traverseFold(const FoldNode& foldNode, const TraversalConstants& constants, std::span<float> outputExpectedValues) {
    Player winner = foldNode.remainingPlayer;
    float reward = static_cast<float>(foldNode.remainingPlayerReward);

    // The player who did not fold wins the reward, regardless of their hand or the board
    for (float& expectedValue : outputExpectedValues) {
        expectedValue = (winner == constants.traverser) ? reward : -reward;
    }
}

void traverseShowdown(
    const ShowdownNode& showdownNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::span<float>>& rangeWeights,
    std::span<float> outputExpectedValues
) {
    auto doHandsOverlap = [&rules](int player0Index, int player1Index, CardSet board) -> bool {
        CardSet player0Hand = rules.getRangeHands(Player::P0)[player0Index];
        CardSet player1Hand = rules.getRangeHands(Player::P1)[player1Index];

        int individualSize = getSetSize(player0Hand) + getSetSize(player1Hand) + getSetSize(board);
        int combinedSize = getSetSize(player0Hand | player1Hand | board);
        assert(individualSize >= combinedSize);
        return individualSize > combinedSize;
    };

    Player traverser = constants.traverser;
    Player opponent = getOpposingPlayer(traverser);

    // TODO: Complexity can be improved to O(n log n)
    for (int traverserRangeIndex = 0; traverserRangeIndex < rangeWeights[traverser].size(); ++traverserRangeIndex) {
        float traverserExpectedValue = 0.0f;
        float opponentWeightSum = 0.0f;

        for (int opponentRangeIndex = 0; opponentRangeIndex < rangeWeights[opponent].size(); ++opponentRangeIndex) {
            int player0Index = (traverser == Player::P0) ? traverserRangeIndex : opponentRangeIndex;
            int player1Index = (traverser == Player::P1) ? traverserRangeIndex : opponentRangeIndex;

            if (doHandsOverlap(player0Index, player1Index, showdownNode.board)) {
                continue;
            }

            float opponentRangeWeight = rangeWeights[opponent][opponentRangeIndex];
            assert(opponentRangeWeight >= 0.0f);

            switch (rules.getShowdownResult({ player0Index, player1Index }, showdownNode.board)) {
                case ShowdownResult::P0Win: {
                    int multiplier = (traverser == Player::P0) ? 1 : -1;
                    traverserExpectedValue += opponentRangeWeight * multiplier * showdownNode.reward;
                    opponentWeightSum += opponentRangeWeight;
                    break;
                }
                case ShowdownResult::P1Win: {
                    int multiplier = (traverser == Player::P1) ? 1 : -1;
                    traverserExpectedValue += opponentRangeWeight * multiplier * showdownNode.reward;
                    opponentWeightSum += opponentRangeWeight;
                    break;
                }
                case ShowdownResult::Tie:
                    opponentWeightSum += opponentRangeWeight;
                    break;
                default:
                    assert(false);
                    break;
            }
        }

        // TODO: Add assert back
        // assert(opponentWeightSum > 0.0f);
        if (opponentWeightSum > 0.0f) {
            outputExpectedValues[traverserRangeIndex] = traverserExpectedValue / opponentWeightSum;
        }
        else {
            outputExpectedValues[traverserRangeIndex] = 0.0f;
        }

    }
}

void traverseTree(
    const Node& node,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::span<float>>& rangeWeights,
    Tree& tree,
    std::span<float> outputExpectedValues
) {
    assert(tree.isTreeSkeletonBuilt() && tree.isFullTreeBuilt());

    switch (node.getNodeType()) {
        case NodeType::Chance:
            traverseChance(node.chanceNode, constants, rules, rangeWeights, tree, outputExpectedValues);
            break;
        case NodeType::Decision:
            traverseDecision(node.decisionNode, constants, rules, rangeWeights, tree, outputExpectedValues);
            break;
        case NodeType::Fold:
            traverseFold(node.foldNode, constants, outputExpectedValues);
            break;
        case NodeType::Showdown:
            traverseShowdown(node.showdownNode, constants, rules, rangeWeights, outputExpectedValues);
            break;
        default:
            assert(false);
            break;
    }
}

std::span<float> traverseFromRoot(const TraversalConstants& constants, const IGameRules& rules, Tree& tree) {
    std::size_t rootNodeIndex = tree.getRootNodeIndex();

    PlayerArray<std::span<float>> inputWeights = getInputWeightSpans(rootNodeIndex, rules, tree);
    assert(rules.getInitialRangeWeights(Player::P0).size() == inputWeights[Player::P0].size());
    assert(rules.getInitialRangeWeights(Player::P1).size() == inputWeights[Player::P1].size());

    // Copy initial weights into input span
    for (Player player : { Player::P0, Player::P1 }) {
        for (int i = 0; i < rules.getInitialRangeWeights(player).size(); ++i) {
            inputWeights[player][i] = rules.getInitialRangeWeights(player)[i];
        }
    }

    std::span<float> outputExpectedValues = getOutputExpectedValueSpan(rootNodeIndex, constants, rules, tree);

    traverseTree(
        tree.allNodes[rootNodeIndex],
        constants,
        rules,
        inputWeights,
        tree,
        outputExpectedValues
    );

    return outputExpectedValues;
}
} // namespace

DiscountParams getDiscountParams(float alpha, float beta, float gamma, int iteration) {
    float t = static_cast<float>(iteration + 1);
    float a = std::pow(t, alpha);
    float b = std::pow(t, beta);

    return {
        .alphaT = a / (a + 1),
        .betaT = b / (b + 1),
        .gammaT = std::pow(t / (t + 1), gamma)
    };
}

// void vanillaCfr(
//     const IGameRules& rules,
//     Player traverser,
//     PlayerArray<float> weights,
//     const Node& node,
//     Tree& tree
// ) {
//     // Vanilla CFR doesn't need discount params
//     TraversalData data = {
//         .weights = weights,
//         .mode = TraversalMode::VanillaCfr,
//         .traverser = traverser,
//     };
//     static_cast<void>(traverseTree(rules, data, handIndices, node, tree));
// }

// void cfrPlus(
//     const IGameRules& rules,
//     Player traverser,
//     PlayerArray<std::uint16_t> handIndices,
//     PlayerArray<float> weights,
//     const Node& node,
//     Tree& tree
// ) {
//     // CFR+ doesn't need discount params
//     TraversalData data = {
//         .weights = weights,
//         .mode = TraversalMode::CfrPlus,
//         .traverser = traverser,
//     };
//     static_cast<void>(traverseTree(rules, data, handIndices, node, tree));
// }

void discountedCfr(
    Player traverser,
    const IGameRules& rules,
    const DiscountParams& params,
    Tree& tree
) {
    TraversalConstants constants = {
        .traverser = traverser,
        .mode = TraversalMode::DiscountedCfr,
        .params = params
    };

    static_cast<void>(traverseFromRoot(constants, rules, tree));
}

float expectedValue(
    Player traverser,
    const IGameRules& rules,
    Tree& tree
) {
    TraversalConstants constants = {
        .traverser = traverser,
        .mode = TraversalMode::ExpectedValue
    };

    std::span<float> expectedValueRange = traverseFromRoot(constants, rules, tree);
    const auto& traverserRangeWeights = rules.getInitialRangeWeights(traverser);
    assert(expectedValueRange.size() == traverserRangeWeights.size());

    float expectedValue = 0.0f;
    for (int i = 0; i < expectedValueRange.size(); ++i) {
        expectedValue += expectedValueRange[i] * traverserRangeWeights[i];
    }
    return expectedValue;
}

FixedVector<float, MaxNumActions> getAverageStrategy(const DecisionNode& decisionNode, int trainingDataSet, const Tree& tree) {
    int numActions = static_cast<int>(decisionNode.decisionDataSize);
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