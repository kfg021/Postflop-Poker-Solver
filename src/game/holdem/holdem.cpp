#include "game/holdem/holdem.hpp"

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "util/fixed_vector.hpp"
#include "util/result.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {
enum class Action : std::uint8_t {
    GameStart,
    DealCard,
    Fold,
    Check,
    Call,
    BetSize0,
    BetSize1,
    BetSize2,
    RaiseSize0,
    RaiseSize1,
    RaiseSize2,
    AllIn
};

bool areWagersValidAfterBetOrRaise(const std::array<int, 2>& playerWagers, int effectiveStack) {
    // Don't allow wagers that would risk more money then we have available
    // Also ignore exact equality since that is identical to an all in
    return (playerWagers[0] < effectiveStack) && (playerWagers[1] < effectiveStack);
}

std::optional<std::array<int, 2>> tryGetWagersAfterBet(
    const std::array<int, 2>& oldPlayerWagers,
    Player bettingPlayer,
    int betPercentage,
    int effectiveStack
) {
    // Before a bet both players should have the same amount wagered
    assert(oldPlayerWagers[0] == oldPlayerWagers[1]);
    int oldPotSize = oldPlayerWagers[0] * 2;

    // Bet a percentage of the pot, rounded up
    int betAmount = (oldPotSize * betPercentage + 99) / 100;

    auto newPlayerWagers = oldPlayerWagers;
    newPlayerWagers[getPlayerID(bettingPlayer)] += betAmount;

    if (areWagersValidAfterBetOrRaise(newPlayerWagers, effectiveStack)) {
        return newPlayerWagers;
    }
    else {
        return std::nullopt;
    }
}

std::optional<std::array<int, 2>> tryGetWagersAfterRaise(
    const std::array<int, 2>& oldPlayerWagers,
    Player raisingPlayer,
    int raisePercentage,
    int effectiveStack
) {
    // Before a raise, the player about to raise must have less wagered
    int oldRaisingPlayerWager = oldPlayerWagers[getPlayerID(raisingPlayer)];
    int oldOpposingPlayerWager = oldPlayerWagers[getOpposingPlayerID(raisingPlayer)];
    int oldRequiredMatchAmount = oldOpposingPlayerWager - oldRaisingPlayerWager;
    assert(oldRequiredMatchAmount > 0);

    // First match the current bet, then bet a percentage on top of that
    std::optional<std::array<int, 2>> newPlayerWagersOption = tryGetWagersAfterBet(
        { oldOpposingPlayerWager, oldOpposingPlayerWager },
        raisingPlayer,
        raisePercentage,
        effectiveStack
    );

    if (!newPlayerWagersOption) {
        return std::nullopt;
    }

    auto& newPlayerWagers = *newPlayerWagersOption;
    int newRaisingPlayerWager = newPlayerWagers[getPlayerID(raisingPlayer)];
    int newOpposingPlayerWager = newPlayerWagers[getOpposingPlayerID(raisingPlayer)];
    int newRequiredMatchAmount = newRaisingPlayerWager - newOpposingPlayerWager;
    assert(newRequiredMatchAmount > 0);

    // By poker rules, we must raise at least the previous raise size
    if ((newRequiredMatchAmount >= oldRequiredMatchAmount) && areWagersValidAfterBetOrRaise(newPlayerWagers, effectiveStack)) {
        return newPlayerWagers;
    }
    else {
        return std::nullopt;
    }
}
} // namespace

Holdem::Holdem(const Settings& settings) : m_settings(settings) {}

GameState Holdem::getInitialGameState() const {
    auto getStartingStreet = [](CardSet communityCards) -> Street {
        switch (getSetSize(communityCards)) {
            case 3:
                return Street::Flop;
            case 4:
                return Street::Turn;
            case 5:
                return Street::River;
            default:
                assert(false);
                return Street::River;
        }
    };

    static const GameState InitialState = {
        .currentBoard = 0,
        .playerTotalWagers = { m_settings.startingPlayerWagers, m_settings.startingPlayerWagers },
        .deadMoney = m_settings.deadMoney,
        .playerToAct = Player::P0,
        .lastAction = static_cast<ActionID>(Action::GameStart),
        .currentStreet = getStartingStreet(m_settings.startingCommunityCards),
    };
    return InitialState;
}

NodeType Holdem::getNodeType(const GameState& state) const {
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
                return (state.currentStreet == Street::River) ? NodeType::Showdown : NodeType::Chance;
            }
            else {
                return NodeType::Decision;
            }

        case Action::Call:
            return (state.currentStreet == Street::River) ? NodeType::Showdown : NodeType::Chance;

        case Action::BetSize0:
        case Action::BetSize1:
        case Action::BetSize2:
        case Action::RaiseSize0:
        case Action::RaiseSize1:
        case Action::RaiseSize2:
        case Action::AllIn:
            return NodeType::Decision;

        default:
            assert(false);
            return NodeType::Fold;
    }
}

ActionType Holdem::getActionType(ActionID actionID) const {
    Action action = static_cast<Action>(actionID);
    assert(action != Action::GameStart);
    return (action == Action::DealCard) ? ActionType::Chance : ActionType::Decision;
}

FixedVector<ActionID, MaxNumActions> Holdem::getValidActions(const GameState& state) const {
    auto addAllValidBetSizes = [this, &state](FixedVector<ActionID, MaxNumActions>& validActions) -> void {
        for (int i = 0; i < m_settings.betSizes.size(); ++i) {
            auto newWagersOption = tryGetWagersAfterBet(
                state.playerTotalWagers,
                state.playerToAct,
                m_settings.betSizes[i],
                m_settings.effectiveStack
            );
            if (newWagersOption) {
                ActionID betID = static_cast<ActionID>(Action::BetSize0) + i;
                validActions.pushBack(betID);
            }
        }
    };

    auto addAllValidRaiseSizes = [this, &state](FixedVector<ActionID, MaxNumActions>& validActions) -> void {
        for (int i = 0; i < m_settings.raiseSizes.size(); ++i) {
            auto newWagersOption = tryGetWagersAfterRaise(
                state.playerTotalWagers,
                state.playerToAct,
                m_settings.raiseSizes[i],
                m_settings.effectiveStack
            );
            if (newWagersOption) {
                ActionID raiseID = static_cast<ActionID>(Action::RaiseSize0) + i;
                validActions.pushBack(raiseID);
            }
        }
    };

    NodeType nodeType = getNodeType(state);
    assert((nodeType == NodeType::Decision) || (nodeType == NodeType::Chance));

    switch (static_cast<Action>(state.lastAction)) {
        case Action::GameStart:
        case Action::DealCard: {
            FixedVector<ActionID, MaxNumActions> validActions = {
                static_cast<ActionID>(Action::Check)
            };
            addAllValidBetSizes(validActions);
            return validActions;
        }

        case Action::Check:
            if (getOpposingPlayer(state.playerToAct) == Player::P1) {
                // Player 1 checked, move to next street
                return {
                    static_cast<ActionID>(Action::DealCard)
                };
            }
            else {
                // Player 0 checked, player 1 can check or bet
                FixedVector<ActionID, MaxNumActions> validActions = {
                    static_cast<ActionID>(Action::Check)
                };
                addAllValidBetSizes(validActions);
                validActions.pushBack(
                    static_cast<ActionID>(Action::AllIn)
                );
                return validActions;
            }

        case Action::Call:
            return {
                static_cast<ActionID>(Action::DealCard)
            };

        case Action::BetSize0:
        case Action::BetSize1:
        case Action::BetSize2:
        case Action::RaiseSize0:
        case Action::RaiseSize1:
        case Action::RaiseSize2: {
            FixedVector<ActionID, MaxNumActions> validActions = {
                static_cast<ActionID>(Action::Fold),
                static_cast<ActionID>(Action::Call)
            };
            addAllValidRaiseSizes(validActions);
            validActions.pushBack(
                static_cast<ActionID>(Action::AllIn)
            );
            return validActions;
        }

        case Action::AllIn:
            return {
                static_cast<ActionID>(Action::Fold),
                static_cast<ActionID>(Action::Call)
            };
    }
}

GameState Holdem::getNewStateAfterDecision(const GameState& state, ActionID actionID) const {
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

        case Action::BetSize0:
        case Action::BetSize1:
        case Action::BetSize2: {
            int betIndex = actionID - static_cast<int>(Action::BetSize0);
            assert(betIndex >= 0 && betIndex < m_settings.betSizes.size());

            auto newWagersOption = tryGetWagersAfterBet(
                state.playerTotalWagers,
                state.playerToAct,
                m_settings.betSizes[betIndex],
                m_settings.effectiveStack
            );
            assert(newWagersOption);

            nextState.playerTotalWagers = *newWagersOption;
            break;
        }

        case Action::RaiseSize0:
        case Action::RaiseSize1:
        case Action::RaiseSize2: {
            int raiseIndex = actionID - static_cast<int>(Action::RaiseSize0);
            assert(raiseIndex >= 0 && raiseIndex < m_settings.raiseSizes.size());

            auto newWagersOption = tryGetWagersAfterRaise(
                state.playerTotalWagers,
                state.playerToAct,
                m_settings.betSizes[raiseIndex],
                m_settings.effectiveStack
            );
            assert(newWagersOption);

            nextState.playerTotalWagers = *newWagersOption;
            break;
        }

        case Action::AllIn:
            // During an all in, the current player bets their entire stack
            nextState.playerTotalWagers[getPlayerID(state.playerToAct)] = m_settings.effectiveStack;
            break;

        default:
            assert(false);
            break;
    }
}

std::uint16_t Holdem::getRangeSize(Player player) const {
    const auto& playerRange = m_settings.playerRanges[getPlayerID(player)];
    return static_cast<std::uint16_t>(playerRange.size());
}

std::vector<InitialSetup> Holdem::getInitialSetups() const {

}

CardSet Holdem::getDeck() const {
    static constexpr CardSet StartingDeck = (1LL << holdem::DeckSize) - 1;
    return StartingDeck & ~m_settings.startingCommunityCards;
}

// ShowdownResult getShowdownResult(const std::array<CardSet, 2>& playerHands, CardSet board) const override;
// std::uint16_t mapHandToIndex(Player player, CardSet hand) const override;
// CardSet mapIndexToHand(Player player, std::uint16_t index) const override;
// std::string getActionName(ActionID actionID) const override;

Holdem::Holdem(const Settings& settings) : m_settings{ settings } {}