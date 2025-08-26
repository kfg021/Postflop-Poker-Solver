#ifndef CFR_HPP
#define CFR_HPP

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "solver/node.hpp"
#include "solver/tree.hpp"
#include "util/fixed_vector.hpp"

#include <cstdint>

struct DiscountParams {
    float alphaT;
    float betaT;
    float gammaT;
};

DiscountParams getDiscountParams(float alpha, float beta, float gamma, int iteration);

// void vanillaCfr

// void cfrPlus(
//     const IGameRules& rules,
//     Player traverser,
//     PlayerArray<std::uint16_t> handIndices,
//     PlayerArray<float> weights,
//     const Node& node,
//     Tree& tree
// );

void discountedCfr(
    Player traverser,
    const IGameRules& rules,
    const DiscountParams& params,
    Tree& tree
);

float expectedValue(
    Player traverser,
    const IGameRules& rules,
    Tree& tree
);

FixedVector<float, MaxNumActions> getAverageStrategy(const DecisionNode& decisionNode, int trainingDataSet, const Tree& tree);

#endif // CFR_HPP