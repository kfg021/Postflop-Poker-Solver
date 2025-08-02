#ifndef KUHN_POKER_HPP
#define KUHN_POKER_HPP

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "util/fixed_vector.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class KuhnPoker final : public IGameRules {
public:
    GameState getInitialGameState() const override;
    NodeType getNodeType(const GameState& state) const override;
    ActionType getActionType(ActionID actionID) const override;
    FixedVector<ActionID, MaxNumActions> getValidActions(const GameState& state) const override;
    GameState getNewStateAfterDecision(const GameState& state, ActionID actionID) const override;
    std::vector<InitialSetup> getInitialSetups() const override;
    CardSet getDeck() const override;
    ShowdownResult getShowdownResult(const std::array<CardSet, 2>& playerHands, CardSet board) const override;
    std::uint16_t mapHandToIndex(Player player, CardSet hand) const override;
    CardSet mapIndexToHand(Player player, std::uint16_t index) const override;
    std::string getActionName(ActionID actionID) const override;
};

#endif // KUHN_POKER_HPP