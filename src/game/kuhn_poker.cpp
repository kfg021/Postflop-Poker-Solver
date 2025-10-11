#include "game/kuhn_poker.hpp"

#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "util/fixed_vector.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {
enum class Action : std::uint8_t {
    GameStart,
    Fold,
    Check,
    Call,
    Bet
};

std::array<CardSet, 3> getHands(const std::array<std::string, 3>& cardNames) {
    std::array<CardSet, 3> hands;
    for (int i = 0; i < 3; ++i) {
        hands[i] = cardIDToSet(getCardIDFromName(cardNames[i]).getValue());
    }
    return hands;
}

// Kuhn poker has [Jack, Queen, King], suits irrelevant
const auto PossibleHands = getHands({ "Js", "Qs", "Ks" });
} // namespace

GameState KuhnPoker::getInitialGameState() const {
    static const GameState InitialState = {
        .currentBoard = 0,
        .totalWagers = { 1, 1 }, // Each player antes 1
        .deadMoney = 0,
        .playerToAct = Player::P0,
        .lastAction = static_cast<ActionID>(Action::GameStart),
        .currentStreet = Street::River, // Since Kuhn poker has one street and no community cards, we begin action on the river 
    };
    return InitialState;
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
        .deadMoney = state.deadMoney,
        .playerToAct = getOpposingPlayer(state.playerToAct),
        .lastAction = actionID,
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

FixedVector<GameState, MaxNumDealCards> KuhnPoker::getNewStatesAfterChance(const GameState& /*state*/) const {
    // Kuhn poker has no chance nodes
    assert(false);
    return {};
}

const std::vector<CardSet>& KuhnPoker::getRangeHands(Player /*player*/) const {
    static const std::vector<CardSet> PossibleHandsVector(PossibleHands.begin(), PossibleHands.end());
    return PossibleHandsVector;
}

const std::vector<float>& KuhnPoker::getInitialRangeWeights(Player /*player*/) const {
    static const std::vector<float> Weights(3, 1.0f);
    return Weights;
}

ShowdownResult KuhnPoker::getShowdownResult(PlayerArray<int> handIndices, CardSet /*board*/) const {
    CardSet player0Hand = PossibleHands[handIndices[Player::P0]];
    CardSet player1Hand = PossibleHands[handIndices[Player::P1]];

    assert(getSetSize(player0Hand) == 1);
    assert(getSetSize(player1Hand) == 1);

    Value player0CardValue = getCardValue(getLowestCardInSet(player0Hand));
    Value player1CardValue = getCardValue(getLowestCardInSet(player1Hand));
    assert(player0CardValue != player1CardValue);

    return (player0CardValue > player1CardValue) ? ShowdownResult::P0Win : ShowdownResult::P1Win;
}

std::string KuhnPoker::getActionName(ActionID actionID) const {
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