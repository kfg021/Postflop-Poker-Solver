#ifndef CFR_HPP
#define CFR_HPP

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "solver/tree.hpp"

#include <array>
#include <cstddef>

float cfr(
    const IGameRules& rules,
    const std::array<CardSet, 2>& playerHands,
    const std::array<float, 2>& playerWeights,
    const Node& node,
    Tree& tree
);

#endif // CFR_HPP