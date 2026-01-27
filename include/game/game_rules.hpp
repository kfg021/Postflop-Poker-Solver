#ifndef GAME_RULES_HPP
#define GAME_RULES_HPP

#include "game/game_types.hpp"
#include "util/fixed_vector.hpp"

#include <span>
#include <string>

class IGameRules {
public:
    virtual ~IGameRules() = default;

    // Functions for building the game tree
    virtual GameState getInitialGameState() const = 0;
    virtual int getDeadMoney() const = 0;
    virtual NodeType getNodeType(const GameState& state) const = 0;
    virtual FixedVector<ActionID, MaxNumActions> getValidActions(const GameState& state) const = 0;
    virtual GameState getNewStateAfterDecision(const GameState& state, ActionID actionID) const = 0;
    virtual ChanceNodeInfo getChanceNodeInfo(CardSet board) const = 0;
    virtual std::span<const CardSet> getRangeHands(Player player) const = 0;

    // Functions for the CFR algorithm
    virtual std::span<const float> getInitialRangeWeights(Player player) const = 0;
    virtual std::span<const std::int16_t> getValidHandIndices(Player player, CardSet board) const = 0;
    virtual std::span<const HandData> getValidSortedHandRanks(Player player, CardSet board) const = 0;
    virtual int getHandIndexAfterSuitSwap(Player player, int handIndex, Suit x, Suit y) const = 0;

    // Functions for output
    virtual std::string getActionName(ActionID actionID, int betRaiseSize) const = 0;
};

#endif // GAME_RULES_HPP