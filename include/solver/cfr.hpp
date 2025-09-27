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

void vanillaCfr(
    Player hero,
    const IGameRules& rules,
    Tree& tree
);

void cfrPlus(
    Player hero,
    const IGameRules& rules,
    Tree& tree
);

void discountedCfr(
    Player hero,
    const IGameRules& rules,
    const DiscountParams& params,
    Tree& tree
);

float expectedValue(
    Player hero,
    const IGameRules& rules,
    Tree& tree
);

std::vector<std::vector<float>> getAverageStrategy(const IGameRules& rules, const DecisionNode& decisionNode, const Tree& tree);

#endif // CFR_HPP