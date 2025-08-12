#ifndef CFR_HPP
#define CFR_HPP

#include "game/game_rules.hpp"
#include "solver/node.hpp"
#include "solver/tree.hpp"

#include <array>
#include <cstdint>

float cfr(
    const IGameRules& rules,
    const std::array<std::uint16_t, 2>& playerHandIndices,
    const std::array<float, 2>& playerWeights,
    const Node& node,
    Tree& tree
);

#endif // CFR_HPP