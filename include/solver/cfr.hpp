#ifndef CFR_HPP
#define CFR_HPP

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "solver/node.hpp"
#include "solver/tree.hpp"
#include "util/fixed_vector.hpp"
#include "util/stack_allocator.hpp"

#include <cstdint>

struct DiscountParams {
    double alphaT;
    double betaT;
    double gammaT;
};

DiscountParams getDiscountParams(float alpha, float beta, float gamma, int iteration);

void vanillaCfr(
    Player hero,
    const IGameRules& rules,
    Tree& tree,
    StackAllocator<float>& allocator
);

void cfrPlus(
    Player hero,
    const IGameRules& rules,
    Tree& tree,
    StackAllocator<float>& allocator
);

void discountedCfr(
    Player hero,
    const IGameRules& rules,
    const DiscountParams& params,
    Tree& tree,
    StackAllocator<float>& allocator
);

float expectedValue(
    Player hero,
    const IGameRules& rules,
    Tree& tree,
    StackAllocator<float>& allocator
);

float bestResponseEV(
    Player hero,
    const IGameRules& rules,
    Tree& tree,
    StackAllocator<float>& allocator
);

float calculateExploitability(const IGameRules& rules, Tree& tree, StackAllocator<float>& allocator);

float calculateExploitabilityFast(const IGameRules& rules, Tree& tree, StackAllocator<float>& allocator);

FixedVector<float, MaxNumActions> getAverageStrategy(int hand, const DecisionNode& decisionNode, const Tree& tree);

std::size_t getTrainingDataIndex(int action, int hand, const DecisionNode& decisionNode, const Tree& tree);

#endif // CFR_HPP