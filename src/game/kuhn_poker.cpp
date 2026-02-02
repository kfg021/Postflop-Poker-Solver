#include "game/kuhn_poker.hpp"

#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "util/fixed_vector.hpp"

#include <cassert>
#include <cstdint>
#include <string>

namespace {
enum class Action : std::uint8_t {
    GameStart,
    Fold,
    Check,
    Call,
    Bet
};

CardSet getHand(Value value, Suit suit) {
    return cardIDToSet(getCardIDFromValueAndSuit(value, suit));
}

const std::array<CardSet, 3> PossibleHands = {
    getHand(Value::Jack, Suit::Spades),
    getHand(Value::Queen, Suit::Spades),
    getHand(Value::King, Suit::Spades)
};
} // namespace

GameState KuhnPoker::getInitialGameState() const {
    static const GameState InitialState = {
        .currentBoard = 0,
        .totalWagers = { 1, 1 }, // Each player antes 1
        .previousStreetsWager = 1,
        .playerToAct = Player::P0,
        .lastAction = static_cast<ActionID>(Action::GameStart),
        .lastDealtCard = InvalidCard,
        .currentStreet = Street::River, // Since Kuhn poker has one street and no community cards, we begin action on the river 
    };
    return InitialState;
}

CardSet KuhnPoker::getDeck() const {
    static const CardSet Deck = PossibleHands[0] | PossibleHands[1] | PossibleHands[2];
    assert(getSetSize(Deck) == 3);
    return Deck;
}

int KuhnPoker::getDeadMoney() const {
    // Kuhn poker has no dead money
    return 0;
}

NodeType KuhnPoker::getNodeType(const GameState& state) const {
    switch (static_cast<Action>(state.lastAction)) {
        case Action::GameStart:
            // Start of game, next player can decide to check / bet
            return NodeType::Decision;
        case Action::Fold:
            // Last player folded, action is over
            return NodeType::Fold;
        case Action::Check:
            // If Player 1 was the one who checked, then the action is over
            return (getOpposingPlayer(state.playerToAct) == Player::P1) ? NodeType::Showdown : NodeType::Decision;
        case Action::Call:
            return NodeType::Showdown;
        case Action::Bet:
            // Next player can decide to call / fold
            return NodeType::Decision;
        default:
            assert(false);
            return NodeType::Fold;
    }
}

FixedVector<ActionID, MaxNumActions>  KuhnPoker::getValidActions(const GameState& state) const {
    assert(getNodeType(state) == NodeType::Decision);

    switch (static_cast<Action>(state.lastAction)) {
        case Action::GameStart:
        case Action::Check:
            return {
                static_cast<ActionID>(Action::Check),
                static_cast<ActionID>(Action::Bet)
            };
        case Action::Bet:
            return {
                static_cast<ActionID>(Action::Fold),
                static_cast<ActionID>(Action::Call)
            };
        default:
            assert(false);
            return {};
    }
}

GameState KuhnPoker::getNewStateAfterDecision(const GameState& state, ActionID actionID) const {
    assert(getNodeType(state) == NodeType::Decision);

    GameState nextState = {
        .currentBoard = state.currentBoard,
        .totalWagers = state.totalWagers,
        .previousStreetsWager = state.previousStreetsWager,
        .playerToAct = getOpposingPlayer(state.playerToAct),
        .lastAction = actionID,
        .lastDealtCard = state.lastDealtCard,
        .currentStreet = state.currentStreet,
    };

    switch (static_cast<Action>(actionID)) {
        case Action::Fold:
        case Action::Check:
            break;
        case Action::Call:
        case Action::Bet:
            // A bet or a call increases the player's wager by 1
            ++nextState.totalWagers[state.playerToAct];
            break;
        default:
            assert(false);
            break;
    }

    return nextState;
}

FixedVector<SuitEquivalenceClass, 4> KuhnPoker::getChanceNodeIsomorphisms(CardSet /*board*/) const {
    // Kuhn poker has no chance nodes
    return {};
}

std::span<const CardSet> KuhnPoker::getRangeHands(Player /*player*/) const {
    return PossibleHands;
}

std::span<const float> KuhnPoker::getInitialRangeWeights(Player /*player*/) const {
    static constexpr std::array<float, 3> Weights = { 1.0f, 1.0f, 1.0f };
    return Weights;
}

std::span<const std::int16_t> KuhnPoker::getValidHandIndices(Player /*player*/, CardSet /*board*/) const {
    static constexpr std::array<const std::int16_t, 3> ValidIndices = { 0, 1, 2 };
    return ValidIndices;
}

std::span<const HandData> KuhnPoker::getValidSortedHandRanks(Player /*player*/, CardSet /*board*/) const {
    enum KuhnHandID : std::uint8_t {
        Jack,
        Queen,
        King
    };

    static constexpr std::array<HandData, 3> SortedHandRanks = {
        HandData{.rank = KuhnHandID::Jack, .index = KuhnHandID::Jack},
        HandData{.rank = KuhnHandID::Queen, .index = KuhnHandID::Queen},
        HandData{.rank = KuhnHandID::King, .index = KuhnHandID::King},
    };

    return SortedHandRanks;
}

int KuhnPoker::getHandIndexAfterSuitSwap(Player /*player*/, int handIndex, Suit /*x*/, Suit /*y*/) const {
    // Kuhn poker only has one suit and no isomorphisms
    return -1;
}

std::string KuhnPoker::getActionName(ActionID actionID, int /*betRaiseSize*/) const {
    switch (static_cast<Action>(actionID)) {
        case Action::Fold:
            return "Fold";
        case Action::Check:
            return "Check";
        case Action::Call:
            return "Call";
        case Action::Bet:
            return "Bet";
        default:
            assert(false);
            return "???";
    }
}