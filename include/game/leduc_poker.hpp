#ifndef LEDUC_POKER_HPP
#define LEDUC_POKER_HPP

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "util/fixed_vector.hpp"

#include <span>
#include <string>

class LeducPoker final : public IGameRules {
public:
    LeducPoker(bool useIsomorphism);

    GameState getInitialGameState() const override;
    int getDeadMoney() const override;
    NodeType getNodeType(const GameState& state) const override;
    FixedVector<ActionID, MaxNumActions> getValidActions(const GameState& state) const override;
    GameState getNewStateAfterDecision(const GameState& state, ActionID actionID) const override;
    ChanceNodeInfo getChanceNodeInfo(CardSet board) const override;
    std::span<const CardSet> getRangeHands(Player player) const override;
    std::span<const float> getInitialRangeWeights(Player player) const override;
    std::span<const HandData> getSortedHandRanks(Player player, CardSet board) const override;
    int getHandIndexAfterSuitSwap(Player player, int handIndex, Suit x, Suit y) const override;
    std::string getActionName(ActionID actionID, int betRaiseSize) const override;

private:
    bool m_useIsomorphism;
};

#endif // LEDUC_POKER_HPP