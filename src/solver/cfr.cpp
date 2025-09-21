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
#include <vector>
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
    Player hero;
    TraversalMode mode;
    DiscountParams params;
};

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

FixedVector<float, MaxNumActions> getCurrentStrategy(const DecisionNode& decisionNode, int trainingDataSet, Tree& tree) {
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

std::vector<float> traverseTree(
    const Node& node,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::vector<float>>& rangeWeights,
    Tree& tree
);

std::vector<float> traverseChance(
    const ChanceNode& chanceNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::vector<float>>& rangeWeights,
    Tree& tree
) {
    // TODO: Implement
    return {};
}

std::vector<float> traverseDecision(
    const DecisionNode& decisionNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::vector<float>>& rangeWeights,
    Tree& tree
) {
    int numActions = static_cast<int>(decisionNode.decisionDataSize);

    Player playerToAct = decisionNode.player;
    int playerToActRangeSize = rangeWeights[decisionNode.player].size();
    std::vector<FixedVector<float, MaxNumActions>> strategies(playerToActRangeSize);

    bool isTraining = (constants.mode != TraversalMode::ExpectedValue);
    if (isTraining) {
        for (int i = 0; i < playerToActRangeSize; ++i) {
            strategies[i] = getCurrentStrategy(decisionNode, i, tree);
        }
    }
    else {
        for (int i = 0; i < playerToActRangeSize; ++i) {
            strategies[i] = getAverageStrategy(decisionNode, i, tree);
        }
    }

    std::vector<float> expectedValues(playerToActRangeSize, 0.0f);

    if (playerToAct == constants.hero) {
        // TODO: Strategy and regret updates

        for (int action = 0; action < numActions; ++action) {
            PlayerArray<std::vector<float>> newRangeWeights = rangeWeights;
            for (int i = 0; i < playerToActRangeSize; ++i) {
                newRangeWeights[playerToAct][i] *= strategies[i][action];
            }

            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            std::vector<float> actionExpectedValues = traverseTree(nextNode, constants, rules, newRangeWeights, tree);
        }
    }
    else {
        // Not the hero's turn; no strategy or regret updates
        for (int action = 0; action < numActions; ++action) {
            PlayerArray<std::vector<float>> newRangeWeights = rangeWeights;
            for (int i = 0; i < playerToActRangeSize; ++i) {
                newRangeWeights[playerToAct][i] *= strategies[i][action];
            }

            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            std::vector<float> actionExpectedValues = traverseTree(nextNode, constants, rules, newRangeWeights, tree);
            for (int i = 0; i < playerToActRangeSize; ++i) {
                expectedValues[i] += actionExpectedValues[action];
            }
        }
    }

    return expectedValues;
}

std::vector<float> traverseFold(const FoldNode& foldNode, const TraversalConstants& constants, const IGameRules& rules) {
    // The expected value of a fold depends only on the size of the pot
    float reward = foldNode.remainingPlayerReward;
    float payoff = (constants.hero == foldNode.remainingPlayer) ? static_cast<float>(reward) : -static_cast<float>(reward);
    int heroRangeSize = rules.getInitialRangeWeights(constants.hero).size();
    return std::vector<float>(heroRangeSize, payoff);
}

std::vector<float> traverseShowdown(
    const ShowdownNode& showdownNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::vector<float>>& rangeWeights
) {
    auto areHandsDisjoint = [&showdownNode, &constants, &rules](int heroIndex, int villianIndex) -> bool {
        CardSet heroHand = rules.mapIndexToHand(constants.hero, heroIndex);
        CardSet villianHand = rules.mapIndexToHand(getOpposingPlayer(constants.hero), villianIndex);
        CardSet board = showdownNode.board;

        int individualSize = getSetSize(heroHand) + getSetSize(villianHand) + getSetSize(board);
        int combinedSize = getSetSize(heroHand | villianHand | board);
        assert(individualSize >= combinedSize);

        return individualSize == combinedSize;
    };

    auto getMultiplier = [&showdownNode, &constants, &rules](int heroIndex, int villianIndex) -> int {
        PlayerArray<int> playerIndices = (constants.hero == Player::P0) ?
            PlayerArray<int>{ heroIndex, villianIndex } :
            PlayerArray<int>{ villianIndex, heroIndex };

        switch (rules.getShowdownResult(playerIndices, showdownNode.board)) {
            case ShowdownResult::P0Win:
                return (constants.hero == Player::P0) ? 1 : -1;
            case ShowdownResult::P1Win:
                return (constants.hero == Player::P1) ? 1 : -1;
            case ShowdownResult::Tie:
                return 0;
            default:
                assert(false);
                return 0;
        }
    };

    const std::vector<float>& heroWeights = rangeWeights[constants.hero];
    const std::vector<float>& villianWeights = rangeWeights[getOpposingPlayer(constants.hero)];

    int heroRangeSize = heroWeights.size();
    int villianRangeSize = villianWeights.size();

    assert(heroRangeSize == rules.getInitialRangeWeights(constants.hero).size());
    assert(villianRangeSize == rules.getInitialRangeWeights(getOpposingPlayer(constants.hero)).size());

    std::vector<float> expectedValues(heroRangeSize, 0.0f);

    for (int i = 0; i < heroRangeSize; ++i) {
        float villianPossibleHandSum = 0.0f;
        for (int j = 0; j < villianRangeSize; ++j) {
            if (areHandsDisjoint(i, j)) {
                assert(villianWeights[j] >= 0.0f);
                villianPossibleHandSum += villianWeights[j];
            }
        }

        assert(villianPossibleHandSum >= 0.0f);
        if (villianPossibleHandSum > 0.0f) {
            for (int j = 0; j < villianRangeSize; ++j) {
                if (areHandsDisjoint(i, j)) {
                    float matchupProbability = villianWeights[j] / villianPossibleHandSum;
                    expectedValues[i] += static_cast<float>(getMultiplier(i, j)) * showdownNode.reward * matchupProbability;
                }
            }
        }
    }

    return expectedValues;
}

std::vector<float> traverseTree(
    const Node& node,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::vector<float>>& rangeWeights,
    Tree& tree
) {
    assert(tree.isTreeSkeletonBuilt() && tree.isFullTreeBuilt());

    switch (node.getNodeType()) {
        case NodeType::Chance:
            return traverseChance(node.chanceNode, constants, rules, rangeWeights, tree);
        case NodeType::Decision:
            return traverseDecision(node.decisionNode, constants, rules, rangeWeights, tree);
        case NodeType::Fold:
            return traverseFold(node.foldNode, constants, rules);
        case NodeType::Showdown:
            return traverseShowdown(node.showdownNode, constants, rules, rangeWeights);
        default:
            assert(false);
            return {};
    }
}

std::vector<float> traverseFromRoot(const TraversalConstants& constants, const IGameRules& rules, Tree& tree) {
    std::size_t rootNodeIndex = tree.getRootNodeIndex();
    PlayerArray<std::vector<float>> rangeWeights = { rules.getInitialRangeWeights(Player::P0), rules.getInitialRangeWeights(Player::P1) };
    return traverseTree(
        tree.allNodes[rootNodeIndex],
        constants,
        rules,
        rangeWeights,
        tree
    );
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

void vanillaCfr(
    Player hero,
    const IGameRules& rules,
    Tree& tree
) {
    TraversalConstants constants = {
        .hero = hero,
        .mode = TraversalMode::VanillaCfr,
        .params = {} // No params needed for vanilla CFR
    };

    static_cast<void>(traverseFromRoot(constants, rules, tree));
}

void cfrPlus(
    Player hero,
    const IGameRules& rules,
    Tree& tree
) {
    TraversalConstants constants = {
        .hero = hero,
        .mode = TraversalMode::CfrPlus,
        .params = {} // No params needed for CFR+
    };

    static_cast<void>(traverseFromRoot(constants, rules, tree));
}

void discountedCfr(
    Player hero,
    const IGameRules& rules,
    const DiscountParams& params,
    Tree& tree
) {
    TraversalConstants constants = {
        .hero = hero,
        .mode = TraversalMode::DiscountedCfr,
        .params = params
    };

    static_cast<void>(traverseFromRoot(constants, rules, tree));
}

float expectedValue(
    Player hero,
    const IGameRules& rules,
    Tree& tree
) {
    TraversalConstants constants = {
        .hero = hero,
        .mode = TraversalMode::ExpectedValue
    };

    std::vector<float> expectedValueRange = traverseFromRoot(constants, rules, tree);
    const auto& heroRangeWeights = rules.getInitialRangeWeights(hero);
    assert(expectedValueRange.size() == heroRangeWeights.size());

    float expectedValue = 0.0f;
    for (int i = 0; i < expectedValueRange.size(); ++i) {
        expectedValue += expectedValueRange[i] * heroRangeWeights[i];
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