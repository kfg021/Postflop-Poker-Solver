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

constexpr std::array<CardID, 6> PossibleCards = {
    getCardIDFromValueAndSuit(Value::Jack, Suit::Hearts),
    getCardIDFromValueAndSuit(Value::Jack, Suit::Spades),
    getCardIDFromValueAndSuit(Value::Queen, Suit::Hearts),
    getCardIDFromValueAndSuit(Value::Queen, Suit::Spades),
    getCardIDFromValueAndSuit(Value::King, Suit::Hearts),
    getCardIDFromValueAndSuit(Value::King, Suit::Spades)
};

constexpr std::array<CardSet, 6> PossibleHands = {
    cardIDToSet(PossibleCards[0]),
    cardIDToSet(PossibleCards[1]),
    cardIDToSet(PossibleCards[2]),
    cardIDToSet(PossibleCards[3]),
    cardIDToSet(PossibleCards[4]),
    cardIDToSet(PossibleCards[5])
};

constexpr HandInfo getHandInfo(int handIndexID) {
    return { .index = static_cast<std::int16_t>(handIndexID), .card0 = PossibleCards[handIndexID], .card1 = InvalidCard };
}
} // namespace

LeducPoker::LeducPoker(bool useChanceCardIsomorphism) : m_useChanceCardIsomorphism{ useChanceCardIsomorphism } {};

GameState LeducPoker::getInitialGameState() const {
    static const GameState InitialState = {
        .currentBoard = 0,
        .totalWagers = { 1, 1 }, // Each player antes 1
        .previousStreetsWager = 1,
        .playerToAct = Player::P0,
        .lastAction = static_cast<ActionID>(Action::StreetStart),
        .lastDealtCard = InvalidCard,
        .currentStreet = Street::Turn, // Since Leduc poker has one street, we begin action on the turn 
    };
    return InitialState;
}

CardSet LeducPoker::getDeck() const {
    static const CardSet Deck = PossibleHands[0]
        | PossibleHands[1]
        | PossibleHands[2]
        | PossibleHands[3]
        | PossibleHands[4]
        | PossibleHands[5];
    assert(getSetSize(Deck) == 6);
    return Deck;
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
        .lastDealtCard = state.lastDealtCard,
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

FixedVector<SuitEquivalenceClass, 4> LeducPoker::getChanceNodeIsomorphisms(CardSet board) const {
    assert(board == 0);

    return m_useChanceCardIsomorphism ?
        FixedVector<SuitEquivalenceClass, 4>{{ Suit::Hearts, Suit::Spades }} :
        FixedVector<SuitEquivalenceClass, 4>{};
}

std::span<const CardSet> LeducPoker::getRangeHands(Player /*player*/) const {
    return PossibleHands;
}

std::span<const float> LeducPoker::getInitialRangeWeights(Player /*player*/) const {
    static constexpr std::array<float, 6> Weights = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    return Weights;
}

std::span<const HandInfo> LeducPoker::getValidHands(Player /*player*/, CardSet board) const {
    auto getHandArray = []() constexpr -> std::array<HandInfo, 6> {
        std::array<HandInfo, 6> validIndices;
        for (int i = 0; i < 6; ++i) {
            validIndices[i] = getHandInfo(i);
        }
        return validIndices;
    };

    auto getHandArrayWithoutIndex = [](int indexToSkip) constexpr -> std::array<HandInfo, 5> {
        std::array<HandInfo, 5> validIndices;
        int currentIndex = 0;
        for (int i = 0; i < 6; ++i) {
            if (i != indexToSkip) {
                validIndices[currentIndex] = getHandInfo(i);
                ++currentIndex;
            }
        }
        return validIndices;
    };

    if (board == 0) {
        static constexpr std::array<HandInfo, 6> ValidIndicesWithoutBoard = getHandArray();
        return ValidIndicesWithoutBoard;
    }
    else {
        static constexpr std::array<std::array<HandInfo, 5>, 6> ValidIndicesWithBoard = {
            getHandArrayWithoutIndex(0), // Board = Jh
            getHandArrayWithoutIndex(1), // Board = Js
            getHandArrayWithoutIndex(2), // Board = Qh
            getHandArrayWithoutIndex(3), // Board = Qs
            getHandArrayWithoutIndex(4), // Board = Kh
            getHandArrayWithoutIndex(5)  // Board = Ks
        };

        assert(getSetSize(board) == 1);

        for (int i = 0; i < 6; ++i) {
            if (board == PossibleHands[i]) {
                return ValidIndicesWithBoard[i];
            }
        }

        assert(false);
        return {};
    }
}

std::span<const RankedHand> LeducPoker::getValidSortedHandRanks(Player /*player*/, CardSet board) const {
    enum LeducHandRankID : std::uint8_t {
        JackHigh,
        QueenHigh,
        KingHigh,
        PairOfJacks,
        PairOfQueens,
        PairOfKings
    };

    enum LeducHandIndexID : std::uint8_t {
        JackOfHearts,
        JackOfSpades,
        QueenOfHearts,
        QueenOfSpades,
        KingOfHearts,
        KingOfSpades
    };

    static constexpr std::array<std::array<RankedHand, 5>, 6> SortedHandRanks = {
        // Board = Jh
        std::array<RankedHand, 5> {
            RankedHand{.rank = LeducHandRankID::QueenHigh, .info = getHandInfo(LeducHandIndexID::QueenOfHearts)},
            RankedHand{.rank = LeducHandRankID::QueenHigh, .info = getHandInfo(LeducHandIndexID::QueenOfSpades)},
            RankedHand{.rank = LeducHandRankID::KingHigh, .info = getHandInfo(LeducHandIndexID::KingOfHearts)},
            RankedHand{.rank = LeducHandRankID::KingHigh, .info = getHandInfo(LeducHandIndexID::KingOfSpades)},
            RankedHand{.rank = LeducHandRankID::PairOfJacks, .info = getHandInfo(LeducHandIndexID::JackOfSpades)}
        },

        // Board = Js
        std::array<RankedHand, 5> {
            RankedHand{.rank = LeducHandRankID::QueenHigh, .info = getHandInfo(LeducHandIndexID::QueenOfHearts)},
            RankedHand{.rank = LeducHandRankID::QueenHigh, .info = getHandInfo(LeducHandIndexID::QueenOfSpades)},
            RankedHand{.rank = LeducHandRankID::KingHigh, .info = getHandInfo(LeducHandIndexID::KingOfHearts)},
            RankedHand{.rank = LeducHandRankID::KingHigh, .info = getHandInfo(LeducHandIndexID::KingOfSpades)},
            RankedHand{.rank = LeducHandRankID::PairOfJacks, .info = getHandInfo(LeducHandIndexID::JackOfHearts)}
        },

        // Board = Qh
        std::array<RankedHand, 5> {
            RankedHand{.rank = LeducHandRankID::JackHigh, .info = getHandInfo(LeducHandIndexID::JackOfHearts)},
            RankedHand{.rank = LeducHandRankID::JackHigh, .info = getHandInfo(LeducHandIndexID::JackOfSpades)},
            RankedHand{.rank = LeducHandRankID::KingHigh, .info = getHandInfo(LeducHandIndexID::KingOfHearts)},
            RankedHand{.rank = LeducHandRankID::KingHigh, .info = getHandInfo(LeducHandIndexID::KingOfSpades)},
            RankedHand{.rank = LeducHandRankID::PairOfQueens, .info = getHandInfo(LeducHandIndexID::QueenOfSpades)}
        },

        // Board = Qs
        std::array<RankedHand, 5>{
            RankedHand{.rank = LeducHandRankID::JackHigh, .info = getHandInfo(LeducHandIndexID::JackOfHearts)},
            RankedHand{.rank = LeducHandRankID::JackHigh, .info = getHandInfo(LeducHandIndexID::JackOfSpades)},
            RankedHand{.rank = LeducHandRankID::KingHigh, .info = getHandInfo(LeducHandIndexID::KingOfHearts)},
            RankedHand{.rank = LeducHandRankID::KingHigh, .info = getHandInfo(LeducHandIndexID::KingOfSpades)},
            RankedHand{.rank = LeducHandRankID::PairOfQueens, .info = getHandInfo(LeducHandIndexID::QueenOfHearts)}
        },

        // Board = Kh
        std::array<RankedHand, 5> {
            RankedHand{.rank = LeducHandRankID::JackHigh, .info = getHandInfo(LeducHandIndexID::JackOfHearts)},
            RankedHand{.rank = LeducHandRankID::JackHigh, .info = getHandInfo(LeducHandIndexID::JackOfSpades)},
            RankedHand{.rank = LeducHandRankID::QueenHigh, .info = getHandInfo(LeducHandIndexID::QueenOfHearts)},
            RankedHand{.rank = LeducHandRankID::QueenHigh, .info = getHandInfo(LeducHandIndexID::QueenOfSpades)},
            RankedHand{.rank = LeducHandRankID::PairOfKings, .info = getHandInfo(LeducHandIndexID::KingOfSpades)}
        },

        // Board = Ks
        std::array<RankedHand, 5> {
            RankedHand{.rank = LeducHandRankID::JackHigh, .info = getHandInfo(LeducHandIndexID::JackOfHearts)},
            RankedHand{.rank = LeducHandRankID::JackHigh, .info = getHandInfo(LeducHandIndexID::JackOfSpades)},
            RankedHand{.rank = LeducHandRankID::QueenHigh, .info = getHandInfo(LeducHandIndexID::QueenOfHearts)},
            RankedHand{.rank = LeducHandRankID::QueenHigh, .info = getHandInfo(LeducHandIndexID::QueenOfSpades)},
            RankedHand{.rank = LeducHandRankID::PairOfKings, .info = getHandInfo(LeducHandIndexID::KingOfHearts)}
        }
    };

    assert(getSetSize(board) == 1);

    for (int i = 0; i < 6; ++i) {
        if (board == PossibleHands[i]) {
            return SortedHandRanks[i];
        }
    }

    assert(false);
    return {};
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