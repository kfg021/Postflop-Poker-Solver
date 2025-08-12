#include "game/leduc_poker.hpp"

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "util/fixed_vector.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {
enum class Action : std::uint8_t {
    GameStart,
    DealCard,
    Fold,
    Check,
    Call,
    Bet,
    Raise
};

// Leduc poker has two copies each of [Jack, Queen, King]
const std::array<CardSet, 6> PossibleHands = {
    cardIDToSet(getCardIDFromName("Jh")),
    cardIDToSet(getCardIDFromName("Js")),
    cardIDToSet(getCardIDFromName("Qh")),
    cardIDToSet(getCardIDFromName("Qs")),
    cardIDToSet(getCardIDFromName("Kh")),
    cardIDToSet(getCardIDFromName("Ks")),
};
} // namespace

GameState LeducPoker::getInitialGameState() const {
    static const GameState InitialState = {
        .currentBoard = 0,
        .playerTotalWagers = {1, 1}, // Each player antes 1
        .deadMoney = 0,
        .playerToAct = Player::P0,
        .lastAction = static_cast<ActionID>(Action::GameStart),
        .currentStreet = Street::Turn, // Since Leduc poker has one street, we begin action on the turn 
    };
    return InitialState;
}

NodeType LeducPoker::getNodeType(const GameState& state) const {
    switch (static_cast<Action>(state.lastAction)) {
        case Action::GameStart:
        case Action::DealCard:
            // Start of street, next player can decide to check / bet
            return NodeType::Decision;
        case Action::Fold:
            // Last player folded, action is over
            return NodeType::Fold;
        case Action::Check:
            // If player 1 was the one who checked, then the action is over (either a chance or a showdown depending on street)
            // Otherwise, player 1 can check or bet
            if (getOpposingPlayer(state.playerToAct) == Player::P1) {
                assert((state.currentStreet == Street::Turn) || (state.currentStreet == Street::River));
                return (state.currentStreet == Street::Turn) ? NodeType::Chance : NodeType::Showdown;
            }
            else {
                return NodeType::Decision;
            }

        case Action::Call:
            assert((state.currentStreet == Street::Turn) || (state.currentStreet == Street::River));
            return (state.currentStreet == Street::Turn) ? NodeType::Chance : NodeType::Showdown;
        case Action::Bet:
        case Action::Raise:
            // Next player can decide to call / fold / raise
            return NodeType::Decision;
        default:
            assert(false);
            return NodeType::Fold;
    }
}

ActionType LeducPoker::getActionType(ActionID actionID) const {
    Action action = static_cast<Action>(actionID);
    assert(action != Action::GameStart);
    return (action == Action::DealCard) ? ActionType::Chance : ActionType::Decision;
}

FixedVector<ActionID, MaxNumActions> LeducPoker::getValidActions(const GameState& state) const {
    NodeType nodeType = getNodeType(state);
    assert((nodeType == NodeType::Decision) || (nodeType == NodeType::Chance));

    switch (static_cast<Action>(state.lastAction)) {
        case Action::GameStart:
        case Action::DealCard:
            return {
                static_cast<ActionID>(Action::Check),
                static_cast<ActionID>(Action::Bet)
            };
        case Action::Check:
            if (getOpposingPlayer(state.playerToAct) == Player::P1) {
                // Player 1 checked, move to next street
                return {
                    static_cast<ActionID>(Action::DealCard)
                };
            }
            else {
                // Player 0 checked, player 1 can check or bet
                return {
                    static_cast<ActionID>(Action::Check),
                    static_cast<ActionID>(Action::Bet)
                };
            }
        case Action::Call:
            return {
                static_cast<ActionID>(Action::DealCard)
            };
        case Action::Bet:
            return {
                static_cast<ActionID>(Action::Fold),
                static_cast<ActionID>(Action::Call),
                static_cast<ActionID>(Action::Raise)
            };
        case Action::Raise:
            return {
                static_cast<ActionID>(Action::Fold),
                static_cast<ActionID>(Action::Call)
            };
        default:
            assert(false);
            return {};
    }
}

GameState LeducPoker::getNewStateAfterDecision(const GameState& state, ActionID actionID) const {
    assert(getNodeType(state) == NodeType::Decision);

    GameState nextState = {
        .currentBoard = state.currentBoard,
        .playerTotalWagers = state.playerTotalWagers,
        .deadMoney = state.deadMoney,
        .playerToAct = getOpposingPlayer(state.playerToAct),
        .lastAction = actionID,
        .currentStreet = state.currentStreet,
    };

    // Leduc poker betting doubles after the community card is dealt
    int betAmount = (state.currentStreet == Street::Turn) ? 2 : 4;

    switch (static_cast<Action>(actionID)) {
        case Action::Fold:
        case Action::Check:
            break;
        case Action::Call:
        case Action::Bet:
            nextState.playerTotalWagers[getPlayerID(state.playerToAct)] += betAmount;
            break;
        case Action::Raise:
            // A raise matches the previous bet, then bets that amount on top
            nextState.playerTotalWagers[getPlayerID(state.playerToAct)] += 2 * betAmount;
            break;
        default:
            assert(false);
            break;
    }

    return nextState;
}

std::vector<InitialSetup> LeducPoker::getInitialSetups() const {
    std::vector<InitialSetup> initialSetups;
    initialSetups.reserve(30);
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            if (i == j) continue;

            std::array<CardSet, 2> playerHands = { PossibleHands[i], PossibleHands[j] };
            static constexpr std::array<float, 2> PlayerWeights = { 1.0f, 1.0f };
            static constexpr float MatchupProbability = 1.0f / 30.0f;

            initialSetups.emplace_back(
                playerHands,
                PlayerWeights,
                MatchupProbability
            );
        }
    }

    return initialSetups;
}

CardSet LeducPoker::getDeck() const {
    CardSet deckSet = 0;
    for (CardSet hand : PossibleHands) {
        deckSet |= hand;
    }
    return deckSet;
}

ShowdownResult LeducPoker::getShowdownResult(const std::array<CardSet, 2>& playerHands, CardSet board) const {
    assert(getSetSize(playerHands[0]) == 1);
    assert(getSetSize(playerHands[1]) == 1);
    assert(getSetSize(board) == 1);

    Value player0CardValue = getCardValue(getLowestCardInSet(playerHands[0]));
    Value player1CardValue = getCardValue(getLowestCardInSet(playerHands[1]));
    Value boardCardValue = getCardValue(getLowestCardInSet(board));

    if (player0CardValue == boardCardValue) {
        // Pair - P0 wins
        return ShowdownResult::P0Win;
    }
    else if (player1CardValue == boardCardValue) {
        // Pair - P1 wins
        return ShowdownResult::P1Win;
    }
    else if (player0CardValue > player1CardValue) {
        // High card - P0 wins
        return ShowdownResult::P0Win;
    }
    else if (player1CardValue > player0CardValue) {
        // High card - P1 wins
        return ShowdownResult::P1Win;
    }
    else {
        return ShowdownResult::Tie;
    }
}

// TODO: Isomorphism
std::uint16_t LeducPoker::mapHandToIndex(Player /*player*/, CardSet hand) const {
    for (int i = 0; i < 6; ++i) {
        if (PossibleHands[i] == hand) {
            return i;
        }
    }

    assert(false);
    return 0;
}

// TODO: Isomorphism
CardSet LeducPoker::mapIndexToHand(Player /*player*/, std::uint16_t index) const {
    assert(index < 6);
    return PossibleHands[index];
}

std::string LeducPoker::getActionName(ActionID actionID) const {
    switch (static_cast<Action>(actionID)) {
        case Action::DealCard:
            return "DealCard";
        case Action::Fold:
            return "Fold";
        case Action::Check:
            return "Check";
        case Action::Call:
            return "Call";
        case Action::Bet:
            return "Bet";
        case Action::Raise:
            return "Raise";
        default:
            assert(false);
            return "???";
    }
}