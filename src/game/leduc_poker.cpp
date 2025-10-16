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
    StreetStart,
    Fold,
    Check,
    Call,
    Bet,
    Raise
};

std::array<CardSet, 6> getHands(const std::array<std::string, 6>& cardNames) {
    std::array<CardSet, 6> hands;
    for (int i = 0; i < 6; ++i) {
        hands[i] = cardIDToSet(getCardIDFromName(cardNames[i]).getValue());
    }
    return hands;
}

// Leduc poker has two copies each of [Jack, Queen, King]
const auto PossibleHands = getHands({ "Jh", "Js", "Qh", "Qs", "Kh", "Ks" });
} // namespace

GameState LeducPoker::getInitialGameState() const {
    static const GameState InitialState = {
        .currentBoard = 0,
        .totalWagers = { 1, 1 }, // Each player antes 1
        .deadMoney = 0,
        .playerToAct = Player::P0,
        .lastAction = static_cast<ActionID>(Action::StreetStart),
        .currentStreet = Street::Turn, // Since Leduc poker has one street, we begin action on the turn 
    };
    return InitialState;
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

FixedVector<GameState, MaxNumDealCards> LeducPoker::getNewStatesAfterChance(const GameState& state) const {
    assert(getNodeType(state) == NodeType::Chance);
    assert(state.currentBoard == 0);
    assert(state.currentStreet == Street::Turn);

    FixedVector<GameState, MaxNumDealCards> statesAfterChance;

    for (CardSet hand : PossibleHands) {
        GameState newState = {
            .currentBoard = hand,
            .totalWagers = state.totalWagers,
            .deadMoney = 0,
            .playerToAct = Player::P0, // Player 0 always starts a new betting round
            .lastAction = static_cast<ActionID>(Action::StreetStart),
            .currentStreet = Street::River,
        };
        statesAfterChance.pushBack(newState);
    }

    return statesAfterChance;
}

const std::vector<CardSet>& LeducPoker::getRangeHands(Player /*player*/) const {
    static const std::vector<CardSet> PossibleHandsVector(PossibleHands.begin(), PossibleHands.end());
    return PossibleHandsVector;
}

const std::vector<float>& LeducPoker::getInitialRangeWeights(Player /*player*/) const {
    static const std::vector<float> Weights(6, 1.0f);
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

std::string LeducPoker::getActionName(ActionID actionID) const {
    switch (static_cast<Action>(actionID)) {
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