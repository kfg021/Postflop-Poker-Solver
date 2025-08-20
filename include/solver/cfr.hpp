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

void cfrPlus(
    const IGameRules& rules,
    Player traverser,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
);

void discountedCfr(
    const IGameRules& rules,
    const DiscountParams& params,
    Player traverser,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
);

float calculatePlayer0ExpectedValue(
    const IGameRules& rules,
    PlayerArray<std::uint16_t> handIndices,
    const Node& node,
    Tree& tree
);

FixedVector<float, MaxNumActions> getAverageStrategy();

#endif // CFR_HPP