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

// Kuhn poker has [Jack, Queen, King], suits irrelevant
const std::array<CardSet, 3> PossibleHands = {
    cardIDToSet(getCardIDFromName("Js")),
    cardIDToSet(getCardIDFromName("Qs")),
    cardIDToSet(getCardIDFromName("Ks")),
};
} // namespace

GameState KuhnPoker::getInitialGameState() const {
    static const GameState InitialState = {
        .currentBoard = 0,
        .playerTotalWagers = {1, 1}, // Each player antes 1
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

ActionType KuhnPoker::getActionType(ActionID actionID) const {
    // Kuhn poker has no chance nodes
    assert(static_cast<Action>(actionID) != Action::GameStart);
    return ActionType::Decision;
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
        .playerTotalWagers = state.playerTotalWagers,
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
            ++nextState.playerTotalWagers[getPlayerID(state.playerToAct)];
            break;
        default:
            assert(false);
            break;
    }

    return nextState;
}

std::vector<InitialSetup> KuhnPoker::getInitialSetups() const {
    std::vector<InitialSetup> initialSetups;
    initialSetups.reserve(6);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (i == j) continue;

            std::array<CardSet, 2> playerHands = { PossibleHands[i], PossibleHands[j] };
            static constexpr std::array<float, 2> PlayerWeights = { 1.0f, 1.0f };
            static constexpr float MatchupProbability = 1.0f / 6.0f;

            initialSetups.emplace_back(
                playerHands,
                PlayerWeights,
                MatchupProbability
            );
        }
    }

    return initialSetups;
}

CardSet KuhnPoker::getDeck() const {
    CardSet deckSet = 0;
    for (CardSet hand : PossibleHands) {
        deckSet |= hand;
    }
    return deckSet;
}

ShowdownResult KuhnPoker::getShowdownResult(const std::array<CardSet, 2>& playerHands, CardSet /*board*/) const {
    assert(getSetSize(playerHands[0]) == 1);
    assert(getSetSize(playerHands[1]) == 1);

    Value player0CardValue = getCardValue(getLowestCardInSet(playerHands[0]));
    Value player1CardValue = getCardValue(getLowestCardInSet(playerHands[1]));
    assert(player0CardValue != player1CardValue);

    return (player0CardValue > player1CardValue) ? ShowdownResult::P0Win : ShowdownResult::P1Win;
}

std::uint16_t KuhnPoker::mapHandToIndex(Player /*player*/, CardSet hand) const {
    for (int i = 0; i < 3; ++i) {
        if (PossibleHands[i] == hand) {
            return i;
        }
    }

    assert(false);
    return 0;
}

CardSet KuhnPoker::mapIndexToHand(Player /*player*/, std::uint16_t index) const {
    assert(index < 3);
    return PossibleHands[index];
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