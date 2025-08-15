#ifndef CFR_HPP
#define CFR_HPP

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "solver/node.hpp"
#include "solver/tree.hpp"

#include <cstdint>

void cfrPlus(
    const IGameRules& rules,
    Player traverser,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
);

float calculatePlayerExpectedValue(
    const IGameRules& rules,
    Player player,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
);

float calculatePlayerBestResponse(
    const IGameRules& rules,
    Player player,
    PlayerArray<std::uint16_t> handIndices,
    PlayerArray<float> weights,
    const Node& node,
    Tree& tree
);

#endif // CFR_HPP