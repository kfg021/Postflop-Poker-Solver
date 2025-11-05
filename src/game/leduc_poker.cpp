#include "game/leduc_poker.hpp"

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "util/fixed_vector.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <string>

namespace {
enum class Action : std::uint8_t {
    StreetStart,
    Fold,
    Check,
    Call,
    Bet,
    Raise
};

CardSet getHand(Value value, Suit suit) {
    return cardIDToSet(getCardIDFromValueAndSuit(value, suit));
}

const std::array<CardSet, 6> PossibleHands = {
    getHand(Value::Jack, Suit::Hearts),
    getHand(Value::Jack, Suit::Spades),
    getHand(Value::Queen, Suit::Hearts),
    getHand(Value::Queen, Suit::Spades),
    getHand(Value::King, Suit::Hearts),
    getHand(Value::King, Suit::Spades)
};
} // namespace

LeducPoker::LeducPoker(bool useChanceCardIsomorphism) : m_useChanceCardIsomorphism{ useChanceCardIsomorphism } {};

GameState LeducPoker::getInitialGameState() const {
    static const GameState InitialState = {
        .currentBoard = 0,
        .totalWagers = { 1, 1 }, // Each player antes 1
        .previousStreetsWager = 1,
        .playerToAct = Player::P0,
        .lastAction = static_cast<ActionID>(Action::StreetStart),
        .currentStreet = Street::Turn, // Since Leduc poker has one street, we begin action on the turn 
    };
    return InitialState;
}

int LeducPoker::getDeadMoney() const {
    // Leduc poker has no dead money
    return 0;
}

NodeType LeducPoker::getNodeType(const GameState& state) const {
    switch (static_cast<Action>(state.lastAction)) {
        case Action::StreetStart:
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

FixedVector<ActionID, MaxNumActions> LeducPoker::getValidActions(const GameState& state) const {
    NodeType nodeType = getNodeType(state);
    assert((nodeType == NodeType::Decision) || (nodeType == NodeType::Chance));

    switch (static_cast<Action>(state.lastAction)) {
        case Action::StreetStart:
            return {
                static_cast<ActionID>(Action::Check),
                static_cast<ActionID>(Action::Bet)
            };
        case Action::Check:
            // The checking player can only be player 0, because otherwise we would be at a chance node
            assert(getOpposingPlayer(state.playerToAct) == Player::P0);

            // Player 0 checked, player 1 can check or bet
            return {
                static_cast<ActionID>(Action::Check),
                static_cast<ActionID>(Action::Bet)
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
        .totalWagers = state.totalWagers,
        .previousStreetsWager = state.previousStreetsWager,
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
            nextState.totalWagers[state.playerToAct] += betAmount;
            break;
        case Action::Raise:
            // A raise matches the previous bet, then bets that amount on top
            nextState.totalWagers[state.playerToAct] += 2 * betAmount;
            break;
        default:
            assert(false);
            break;
    }

    return nextState;
}

ChanceNodeInfo LeducPoker::getChanceNodeInfo(CardSet board) const {
    assert(board == 0);

    static const CardSet Deck = PossibleHands[0]
        | PossibleHands[1]
        | PossibleHands[2]
        | PossibleHands[3]
        | PossibleHands[4]
        | PossibleHands[5];
    assert(getSetSize(Deck) == 6);

    FixedVector<SuitEquivalenceClass, 4> isomorphisms;
    if (m_useChanceCardIsomorphism) {
        isomorphisms.pushBack({ Suit::Hearts, Suit::Spades });
    }

    return {
        .availableCards = Deck,
        .isomorphisms = isomorphisms
    };
}

std::span<const CardSet> LeducPoker::getRangeHands(Player /*player*/) const {
    return PossibleHands;
}

std::span<const float> LeducPoker::getInitialRangeWeights(Player /*player*/) const {
    static constexpr std::array<float, 6> Weights = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    return Weights;
}

std::span<const HandData> LeducPoker::getSortedHandRanks(Player /*player*/, CardSet board) const {
    enum LeducHandRankID : std::uint8_t {
        JackHigh,
        QueenHigh,
        KingHigh,
        PairOfJacks,
        PairOfQueens,
        PairOfKings
    };

    enum LeducHandIndexID : std::uint8_t {
        Jack0,
        Jack1,
        Queen0,
        Queen1,
        King0,
        King1
    };

    static constexpr std::array<HandData, 6> SortedHandRanksJackBoard = {
        HandData{.rank = LeducHandRankID::QueenHigh, .index = LeducHandIndexID::Queen0},
        HandData{.rank = LeducHandRankID::QueenHigh, .index = LeducHandIndexID::Queen1},
        HandData{.rank = LeducHandRankID::KingHigh, .index = LeducHandIndexID::King0},
        HandData{.rank = LeducHandRankID::KingHigh, .index = LeducHandIndexID::King1},
        HandData{.rank = LeducHandRankID::PairOfJacks, .index = LeducHandIndexID::Jack0},
        HandData{.rank = LeducHandRankID::PairOfJacks, .index = LeducHandIndexID::Jack1},
    };

    static constexpr std::array<HandData, 6> SortedHandRanksQueenBoard = {
        HandData{.rank = LeducHandRankID::JackHigh, .index = LeducHandIndexID::Jack0},
        HandData{.rank = LeducHandRankID::JackHigh, .index = LeducHandIndexID::Jack1},
        HandData{.rank = LeducHandRankID::KingHigh, .index = LeducHandIndexID::King0},
        HandData{.rank = LeducHandRankID::KingHigh, .index = LeducHandIndexID::King1},
        HandData{.rank = LeducHandRankID::PairOfQueens, .index = LeducHandIndexID::Queen0},
        HandData{.rank = LeducHandRankID::PairOfQueens, .index = LeducHandIndexID::Queen1},
    };

    static constexpr std::array<HandData, 6> SortedHandRanksKingBoard = {
        HandData{.rank = LeducHandRankID::JackHigh, .index = LeducHandIndexID::Jack0},
        HandData{.rank = LeducHandRankID::JackHigh, .index = LeducHandIndexID::Jack1},
        HandData{.rank = LeducHandRankID::QueenHigh, .index = LeducHandIndexID::Queen0},
        HandData{.rank = LeducHandRankID::QueenHigh, .index = LeducHandIndexID::Queen1},
        HandData{.rank = LeducHandRankID::PairOfKings, .index = LeducHandIndexID::King0},
        HandData{.rank = LeducHandRankID::PairOfKings, .index = LeducHandIndexID::King1},
    };

    assert(getSetSize(board) == 1);
    Value boardCardValue = getCardValue(getLowestCardInSet(board));

    switch (boardCardValue) {
        case Value::Jack:
            return SortedHandRanksJackBoard;
        case Value::Queen:
            return SortedHandRanksQueenBoard;
        case Value::King:
            return SortedHandRanksKingBoard;
        default:
            assert(false);
            return {};
    }
}

int LeducPoker::getHandIndexAfterSuitSwap(Player /*player*/, int handIndex, Suit x, Suit y) const {
    assert(m_useChanceCardIsomorphism);

    if (x > y) std::swap(x, y);
    assert((x == Suit::Hearts) && (y == Suit::Spades));

    // Leduc poker hands are ordered like [Jh, Js, Qh, Qs, Kh, Ks],
    // so to swap suits we either add or subtract 1 depending on the index
    return handIndex ^ 1;
}

std::string LeducPoker::getActionName(ActionID actionID, int betRaiseSize) const {
    switch (static_cast<Action>(actionID)) {
        case Action::Fold:
            return "Fold";
        case Action::Check:
            return "Check";
        case Action::Call:
            return "Call";
        case Action::Bet:
            return "Bet " + std::to_string(betRaiseSize);
        case Action::Raise:
            return "Raise " + std::to_string(betRaiseSize);
        default:
            assert(false);
            return "???";
    }
}