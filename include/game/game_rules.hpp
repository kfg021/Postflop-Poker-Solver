#ifndef GAME_RULES_HPP
#define GAME_RULES_HPP

#include "game/game_types.hpp"
#include "util/fixed_vector.hpp"

#include <cstdint>
#include <string>
#include <vector>

class IGameRules {
public:
    virtual ~IGameRules() = default;

    // Functions for building the game tree
    virtual GameState getInitialGameState() const = 0;
    virtual NodeType getNodeType(const GameState& state) const = 0;
    virtual ActionType getActionType(ActionID actionID) const = 0;
    virtual FixedVector<ActionID, MaxNumActions> getValidActions(const GameState& state) const = 0;
    virtual GameState getNewStateAfterDecision(const GameState& state, ActionID actionID) const = 0;

    // Functions for the CFR algorithm
    virtual std::vector<InitialSetup> getInitialSetups() const = 0;
    virtual CardSet getDeck() const = 0;
    virtual std::uint16_t mapHandToIndex(Player player, CardSet hand) const = 0;
    virtual ShowdownResult getShowdownResult(const std::array<CardSet, 2>& playerHands, CardSet board) const = 0;

    // Functions for output
    virtual std::string getActionName(ActionID actionID) const = 0;
    virtual std::string getHandName(std::uint16_t handIndex) const = 0;
};

#endif // GAME_RULES_HPP