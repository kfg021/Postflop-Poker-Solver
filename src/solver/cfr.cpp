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
    Player traverser;
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
    // TODO: Implement
    return {};
}

std::vector<float> traverseFold(const FoldNode& foldNode, const TraversalConstants& constants, const IGameRules& rules) {
    // TODO: Implement
    return {};
}

std::vector<float> traverseShowdown(
    const ShowdownNode& showdownNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::vector<float>>& rangeWeights
) {
    // TODO: Implement
    return {};
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
    Player traverser,
    const IGameRules& rules,
    Tree& tree
) {
    TraversalConstants constants = {
        .traverser = traverser,
        .mode = TraversalMode::VanillaCfr,
        .params = {} // No params needed for vanilla CFR
    };

    static_cast<void>(traverseFromRoot(constants, rules, tree));
}

void cfrPlus(
    Player traverser,
    const IGameRules& rules,
    Tree& tree
) {
    TraversalConstants constants = {
        .traverser = traverser,
        .mode = TraversalMode::CfrPlus,
        .params = {} // No params needed for CFR+
    };

    static_cast<void>(traverseFromRoot(constants, rules, tree));
}

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

    std::vector<float> expectedValueRange = traverseFromRoot(constants, rules, tree);
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