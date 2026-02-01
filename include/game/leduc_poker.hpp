#ifndef LEDUC_POKER_HPP
#define LEDUC_POKER_HPP

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "util/fixed_vector.hpp"

#include <span>
#include <string>

class LeducPoker final : public IGameRules {
public:
    LeducPoker(bool useChanceCardIsomorphism);

    GameState getInitialGameState() const override;
    CardSet getDeck() const override;
    int getDeadMoney() const override;
    bool isUsingIsomorphism() const override;
    NodeType getNodeType(const GameState& state) const override;
    FixedVector<ActionID, MaxNumActions> getValidActions(const GameState& state) const override;
    GameState getNewStateAfterDecision(const GameState& state, ActionID actionID) const override;
    FixedVector<SuitEquivalenceClass, 4> getChanceNodeIsomorphisms(CardSet board) const override;
    std::span<const CardSet> getRangeHands(Player player) const override;
    int getHandIndexAfterSuitSwap(Player player, int handIndex, Suit x, Suit y) const override;
    std::span<const float> getInitialRangeWeights(Player player) const override;
    std::span<const std::int16_t> getValidHandIndices(Player player, CardSet board) const override;
    std::span<const HandData> getValidSortedHandRanks(Player player, CardSet board) const override;
    std::string getActionName(ActionID actionID, int betRaiseSize) const override;

private:
    bool m_useChanceCardIsomorphism;
};

#endif // LEDUC_POKER_HPP