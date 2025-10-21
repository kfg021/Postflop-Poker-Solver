#ifndef LEDUC_POKER_HPP
#define LEDUC_POKER_HPP

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "util/fixed_vector.hpp"

#include <span>
#include <string>
#include <vector>

class LeducPoker final : public IGameRules {
public:
    GameState getInitialGameState() const override;
    int getDeadMoney() const override;
    NodeType getNodeType(const GameState& state) const override;
    FixedVector<ActionID, MaxNumActions> getValidActions(const GameState& state) const override;
    GameState getNewStateAfterDecision(const GameState& state, ActionID actionID) const override;
    FixedVector<GameState, MaxNumDealCards> getNewStatesAfterChance(const GameState& state) const override;
    const std::vector<CardSet>& getRangeHands(Player player) const override;
    const std::vector<float>& getInitialRangeWeights(Player player) const override;
    std::span<const HandData> getSortedHandRanks(Player player, CardSet board) const override;
    std::string getActionName(ActionID actionID, int betRaiseSize) const override;
};

#endif // LEDUC_POKER_HPP