#ifndef LEDUC_POKER_HPP
#define LEDUC_POKER_HPP

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "util/fixed_vector.hpp"

#include <cstdint>
#include <string>
#include <vector>

class LeducPoker final : public IGameRules {
public:
    GameState getInitialGameState() const override;
    NodeType getNodeType(const GameState& state) const override;
    FixedVector<ActionID, MaxNumActions> getValidActions(const GameState& state) const override;
    GameState getNewStateAfterDecision(const GameState& state, ActionID actionID) const override;
    FixedVector<GameState, MaxNumDealCards> getNewStatesAfterChance(const GameState& state) const override;
    std::uint16_t getRangeSize(Player player) const override;
    std::vector<InitialSetup> getInitialSetups() const override;
    CardSet getDeck() const override;
    CardSet mapIndexToHand(Player player, std::uint16_t index) const override;
    ShowdownResult getShowdownResult(PlayerArray<std::uint16_t> handIndices, CardSet board) const override;
    std::string getActionName(ActionID actionID) const override;
};

#endif // LEDUC_POKER_HPP