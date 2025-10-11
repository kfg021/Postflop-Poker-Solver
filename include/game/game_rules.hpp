#ifndef GAME_RULES_HPP
#define GAME_RULES_HPP

#include "game/game_types.hpp"
#include "util/fixed_vector.hpp"

#include <string>
#include <vector>

class IGameRules {
public:
    virtual ~IGameRules() = default;

    // Functions for building the game tree
    virtual GameState getInitialGameState() const = 0;
    virtual NodeType getNodeType(const GameState& state) const = 0;
    virtual FixedVector<ActionID, MaxNumActions> getValidActions(const GameState& state) const = 0;
    virtual GameState getNewStateAfterDecision(const GameState& state, ActionID actionID) const = 0;
    virtual FixedVector<GameState, MaxNumDealCards> getNewStatesAfterChance(const GameState& state) const = 0;
    virtual const std::vector<CardSet>& getRangeHands(Player player) const = 0;

    // Functions for the CFR algorithm
    virtual const std::vector<float>& getInitialRangeWeights(Player player) const = 0;
    virtual ShowdownResult getShowdownResult(PlayerArray<int> handIndices, CardSet board) const = 0;

    // Functions for output
    virtual std::string getActionName(ActionID actionID) const = 0;
};

#endif // GAME_RULES_HPP