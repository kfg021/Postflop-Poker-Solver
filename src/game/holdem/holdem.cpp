#include "game/holdem/holdem.hpp"

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "game/holdem/config.hpp"
#include "game/holdem/hand_evaluation.hpp"
#include "util/fixed_vector.hpp"
#include "util/result.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
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

static constexpr int NumPossibleTwoCardRunouts = (holdem::DeckSize * (holdem::DeckSize - 1)) / 2;

int mapTwoCardSetToIndex(CardSet cardSet) {
    assert(getSetSize(cardSet) == 2);

    int card0Index = static_cast<int>(popLowestCardFromSet(cardSet));
    int card1Index = static_cast<int>(popLowestCardFromSet(cardSet));
    assert(cardSet == 0);

    int finalIndex = card0Index + (card1Index * (card1Index - 1)) / 2;
    assert(finalIndex < NumPossibleTwoCardRunouts);
    return finalIndex;
}

std::optional<PlayerArray<int>> tryGetWagersAfterBet(
    PlayerArray<int> oldWagers,
    int deadMoney,
    Player bettingPlayer,
    int betPercentage,
    int effectiveStack
) {
    // Before a bet both players should have the same amount wagered
    assert(oldWagers[Player::P0] == oldWagers[Player::P1]);
    int oldPotSize = oldWagers[Player::P0] * 2 + deadMoney;

    // Bet a percentage of the pot, rounded up
    int betAmount = (oldPotSize * betPercentage + 99) / 100;

    PlayerArray<int> newWagers = oldWagers;
    newWagers[bettingPlayer] += betAmount;

    // Don't allow wagers that would risk more money then we have available
    // Also ignore exact equality since that is identical to an all in
    if ((newWagers[Player::P0] >= effectiveStack) || (newWagers[Player::P1] >= effectiveStack)) {
        return std::nullopt;
    }

    return newWagers;
}

std::optional<PlayerArray<int>> tryGetWagersAfterRaise(
    PlayerArray<int> oldWagers,
    int deadMoney,
    Player raisingPlayer,
    int raisePercentage,
    int effectiveStack
) {
    // Before a raise, the player about to raise must have less wagered
    int oldRaisingPlayerWager = oldWagers[raisingPlayer];
    int oldOpposingPlayerWager = oldWagers[getOpposingPlayer(raisingPlayer)];
    int oldRequiredMatchAmount = oldOpposingPlayerWager - oldRaisingPlayerWager;
    assert(oldRequiredMatchAmount > 0);

    // First match the current bet, then bet a percentage on top of that
    std::optional<PlayerArray<int>> newPlayerWagersOption = tryGetWagersAfterBet(
        { oldOpposingPlayerWager, oldOpposingPlayerWager },
        deadMoney,
        raisingPlayer,
        raisePercentage,
        effectiveStack
    );

    if (!newPlayerWagersOption) {
        return std::nullopt;
    }

    const auto& newPlayerWagers = *newPlayerWagersOption;
    int newRaisingPlayerWager = newPlayerWagers[raisingPlayer];
    int newOpposingPlayerWager = newPlayerWagers[getOpposingPlayer(raisingPlayer)];
    int newRequiredMatchAmount = newRaisingPlayerWager - newOpposingPlayerWager;
    assert(newRequiredMatchAmount > 0);

    // By poker rules, we must raise at least the previous raise size
    if (newRequiredMatchAmount < oldRequiredMatchAmount) {
        return std::nullopt;
    }

    return newPlayerWagers;
}
} // namespace

Holdem::RangeElement::RangeElement(CardSet hand_, int frequency_) : hand{ hand_ }, frequency{ frequency_ } {}

Holdem::Holdem(const Settings& settings) : m_settings{ settings } {
    switch (getSetSize(m_settings.startingCommunityCards)) {
        case 3:
            m_startingStreet = Street::Flop;
            break;
        case 4:
            m_startingStreet = Street::Turn;
            break;
        case 5:
            m_startingStreet = Street::River;
            break;
    }

    buildHandRankTables();
}

GameState Holdem::getInitialGameState() const {
    const GameState initialState = {
        .currentBoard = m_settings.startingCommunityCards,
        .totalWagers = { m_settings.startingPlayerWagers, m_settings.startingPlayerWagers },
        .deadMoney = m_settings.deadMoney,
        .playerToAct = Player::P0,
        .lastAction = static_cast<ActionID>(Action::GameStart),
        .currentStreet = m_startingStreet
    };
    return initialState;
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

        case Action::Call: {
            // After a call both players should have the same amount wagered
            assert(state.totalWagers[Player::P0] == state.totalWagers[Player::P1]);

            // If the previous player called an all in, then we move to showdown / runout regardless of the street
            // Otherwise, if we are at the river we are at a showdown node, and if not we are at a chance node
            bool isAllIn = (state.totalWagers[Player::P0] == getTotalEffectiveStack());
            return (state.currentStreet == Street::River || isAllIn) ? NodeType::Showdown : NodeType::Chance;
        }

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
                state.totalWagers,
                state.deadMoney,
                state.playerToAct,
                m_settings.betSizes[i],
                getTotalEffectiveStack()
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
                state.totalWagers,
                state.deadMoney,
                state.playerToAct,
                m_settings.raiseSizes[i],
                getTotalEffectiveStack()
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
            validActions.pushBack(static_cast<ActionID>(Action::AllIn));
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

        default:
            assert(false);
            return {};
    }
}

GameState Holdem::getNewStateAfterDecision(const GameState& state, ActionID actionID) const {
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

        case Action::Call: {
            int wagerToMatch = state.totalWagers[getOpposingPlayer(state.playerToAct)];
            nextState.totalWagers[state.playerToAct] = wagerToMatch;
            break;
        }

        case Action::BetSize0:
        case Action::BetSize1:
        case Action::BetSize2: {
            int betIndex = actionID - static_cast<int>(Action::BetSize0);
            assert(betIndex >= 0 && betIndex < m_settings.betSizes.size());

            auto newWagersOption = tryGetWagersAfterBet(
                state.totalWagers,
                state.deadMoney,
                state.playerToAct,
                m_settings.betSizes[betIndex],
                getTotalEffectiveStack()
            );
            assert(newWagersOption);

            nextState.totalWagers = *newWagersOption;
            break;
        }

        case Action::RaiseSize0:
        case Action::RaiseSize1:
        case Action::RaiseSize2: {
            int raiseIndex = actionID - static_cast<int>(Action::RaiseSize0);
            assert(raiseIndex >= 0 && raiseIndex < m_settings.raiseSizes.size());

            auto newWagersOption = tryGetWagersAfterRaise(
                state.totalWagers,
                state.deadMoney,
                state.playerToAct,
                m_settings.raiseSizes[raiseIndex],
                getTotalEffectiveStack()
            );
            assert(newWagersOption);

            nextState.totalWagers = *newWagersOption;
            break;
        }

        case Action::AllIn:
            // During an all in, the current player bets their entire stack
            nextState.totalWagers[state.playerToAct] = getTotalEffectiveStack();
            break;

        default:
            assert(false);
            break;
    }

    return nextState;
}

std::uint16_t Holdem::getRangeSize(Player player) const {
    const auto& playerRange = m_settings.ranges[player];
    return static_cast<std::uint16_t>(playerRange.size());
}

std::vector<InitialSetup> Holdem::getInitialSetups() const {
    auto areHandsCompatible = [](CardSet hand0, CardSet hand1, CardSet board) -> bool {
        int totalNumCards = getSetSize(board) + 4;
        return getSetSize(hand0 | hand1 | board) == totalNumCards;
    };

    const auto& player0Range = m_settings.ranges[Player::P0];
    const auto& player1Range = m_settings.ranges[Player::P1];

    std::vector<InitialSetup> initialSetups;
    initialSetups.reserve(player0Range.size() * player1Range.size());

    float totalWeight = 0.0f;
    for (const auto& [hand0, frequency0] : player0Range) {
        for (const auto& [hand1, frequency1] : player1Range) {
            assert(getSetSize(hand0) == 2);
            assert(getSetSize(hand1) == 2);
            assert(frequency0 > 0 && frequency0 <= 100);
            assert(frequency1 > 0 && frequency1 <= 100);

            if (areHandsCompatible(hand0, hand1, m_settings.startingCommunityCards)) {
                float weight = (frequency0 / 100.0f) * (frequency1 / 100.0f);
                totalWeight += weight;
            }
        }
    }

    // TODO: Handle when there are no valid combos (totalWeight == 0)

    for (int i = 0; i < player0Range.size(); ++i) {
        for (int j = 0; j < player1Range.size(); ++j) {
            const auto& [hand0, frequency0] = player0Range[i];
            const auto& [hand1, frequency1] = player1Range[j];

            if (areHandsCompatible(hand0, hand1, m_settings.startingCommunityCards)) {
                float frequency0Float = frequency0 / 100.0f;
                float frequency1Float = frequency1 / 100.0f;
                float weight = frequency0Float * frequency1Float;
                initialSetups.emplace_back(
                    PlayerArray<std::uint16_t>{ i, j },
                    PlayerArray<float>{ frequency0Float, frequency1Float },
                    weight / totalWeight
                );
            }
        }
    }

    return initialSetups;
}

CardSet Holdem::getDeck() const {
    static constexpr CardSet StartingDeck = (1LL << holdem::DeckSize) - 1;
    return StartingDeck & ~m_settings.startingCommunityCards;
}

CardSet Holdem::mapIndexToHand(Player player, std::uint16_t index) const {
    const auto& playerRange = m_settings.ranges[player];
    return playerRange[index].hand;
}

ShowdownResult Holdem::getShowdownResult(PlayerArray<std::uint16_t> handIndices, CardSet board) const {
    CardSet chanceCardsDealt = board & ~m_settings.startingCommunityCards;

    int runoutIndex;
    switch (m_startingStreet) {
        case Street::River:
            assert(getSetSize(chanceCardsDealt) == 0);
            runoutIndex = 0;
            break;
        case Street::Turn:
            assert(getSetSize(chanceCardsDealt) == 1);
            runoutIndex = static_cast<int>(getLowestCardInSet(chanceCardsDealt));
            break;
        case Street::Flop:
            assert(getSetSize(chanceCardsDealt) == 2);
            runoutIndex = mapTwoCardSetToIndex(chanceCardsDealt);
            break;
        default:
            assert(false);
    }

    int player0Index = (runoutIndex * m_settings.ranges[Player::P0].size()) + handIndices[Player::P0];
    int player1Index = (runoutIndex * m_settings.ranges[Player::P1].size()) + handIndices[Player::P1];

    std::uint32_t player0HandRank = m_handRanks[Player::P0][player0Index];
    std::uint32_t player1HandRank = m_handRanks[Player::P1][player1Index];

    // 0 is designed to be an invalid ranking
    assert((player0HandRank != 0) && (player1HandRank != 0));

    if (player0HandRank > player1HandRank) {
        return ShowdownResult::P0Win;
    }
    else if (player1HandRank > player0HandRank) {
        return ShowdownResult::P1Win;
    }
    else {
        return ShowdownResult::Tie;
    }
}

std::string Holdem::getActionName(ActionID actionID) const {
    auto getBetName = [this](int betSizeIndex) -> std::string {
        assert(betSizeIndex < m_settings.betSizes.size());
        int betPercentage = m_settings.betSizes[betSizeIndex];
        return "Bet" + std::to_string(betPercentage) + "%";
    };

    auto getRaiseName = [this](int raiseSizeIndex) -> std::string {
        assert(raiseSizeIndex < m_settings.raiseSizes.size());
        int raisePercentage = m_settings.raiseSizes[raiseSizeIndex];
        return "Raise" + std::to_string(raisePercentage) + "%";
    };

    switch (static_cast<Action>(actionID)) {
        case Action::DealCard:
            return "DealCard";
        case Action::Fold:
            return "Fold";
        case Action::Check:
            return "Check";
        case Action::Call:
            return "Call";
        case Action::BetSize0:
            return getBetName(0);
        case Action::BetSize1:
            return getBetName(1);
        case Action::BetSize2:
            return getBetName(2);
        case Action::RaiseSize0:
            return getRaiseName(0);
        case Action::RaiseSize1:
            return getRaiseName(1);
        case Action::RaiseSize2:
            return getRaiseName(2);
        case Action::AllIn:
            return "AllIn";
        default:
            assert(false);
            return "???";
    }
}

void Holdem::buildHandRankTables() {
    std::unordered_map<CardSet, std::uint32_t> seenFiveCardHandRanks;

    auto insertSevenCardHandRank = [this, &seenFiveCardHandRanks](Player player, CardSet board, int index) -> void {
        assert(getSetSize(board) == 7);

        std::array<CardID, 7> sevenCardArray;
        CardSet temp = board;
        for (int i = 0; i < 7; ++i) {
            sevenCardArray[i] = popLowestCardFromSet(temp);
        }
        assert(temp == 0);

        std::uint32_t handRanking = 0;
        for (int i = 0; i < 7; ++i) {
            for (int j = i + 1; j < 7; ++j) {
                CardSet cardsToIgnore = cardIDToSet(sevenCardArray[i]) | cardIDToSet(sevenCardArray[j]);
                CardSet fiveCardHand = board & ~cardsToIgnore;

                std::uint32_t fiveCardHandRanking;
                auto it = seenFiveCardHandRanks.find(fiveCardHand);
                if (it != seenFiveCardHandRanks.end()) {
                    fiveCardHandRanking = it->second;
                }
                else {
                    fiveCardHandRanking = getFiveCardHandRank(fiveCardHand);
                    seenFiveCardHandRanks.emplace(fiveCardHand, fiveCardHandRanking);
                }

                handRanking = std::max(handRanking, fiveCardHandRanking);
            }
        }

        assert(handRanking != 0);
        m_handRanks[player][index] = handRanking;
    };

    const auto& startingCards = m_settings.startingCommunityCards;
    const auto& ranges = m_settings.ranges;

    // TODO: Consider getting rid of empty spaces in lookup table
    switch (m_startingStreet) {
        case Street::River:
            // We are starting at the river, so we can directly map player range indices into the hand ranking table
            for (Player player : { Player::P0, Player::P1 }) {
                int rangeSize = ranges[player].size();
                int handRankTableSize = ranges[player].size();
                m_handRanks[player].assign(handRankTableSize, 0);

                for (int rangeIndex = 0; rangeIndex < ranges[player].size(); ++rangeIndex) {
                    CardSet board = ranges[player][rangeIndex].hand | startingCards;
                    if (getSetSize(board) != 7) continue;

                    insertSevenCardHandRank(player, board, rangeIndex);
                }
            }
            break;

        case Street::Turn:
            // We are starting at the turn, so we have to consider each possible river runout
            for (Player player : { Player::P0, Player::P1 }) {
                int rangeSize = ranges[player].size();
                int handRankTableSize = holdem::DeckSize * rangeSize;
                m_handRanks[player].assign(handRankTableSize, 0);

                for (CardID riverCard = 0; riverCard < holdem::DeckSize; ++riverCard) {
                    for (int rangeIndex = 0; rangeIndex < rangeSize; ++rangeIndex) {
                        CardSet board = ranges[player][rangeIndex].hand | startingCards | cardIDToSet(riverCard);
                        if (getSetSize(board) != 7) continue;

                        int handRankIndex = (static_cast<int>(riverCard) * rangeSize) + rangeIndex;
                        insertSevenCardHandRank(player, board, handRankIndex);
                    }
                }
            }
            break;

        case Street::Flop:
            // We are starting at the flop, so we have to consider each possible turn and river runout
            for (Player player : { Player::P0, Player::P1 }) {
                int rangeSize = ranges[player].size();
                int handRankTableSize = NumPossibleTwoCardRunouts * rangeSize;
                m_handRanks[player].assign(handRankTableSize, 0);

                for (CardID turnCard = 0; turnCard < holdem::DeckSize; ++turnCard) {
                    for (CardID riverCard = turnCard + 1; riverCard < holdem::DeckSize; ++riverCard) {
                        for (int rangeIndex = 0; rangeIndex < rangeSize; ++rangeIndex) {
                            CardSet runout = cardIDToSet(turnCard) | cardIDToSet(riverCard);
                            CardSet board = ranges[player][rangeIndex].hand | startingCards | runout;
                            if (getSetSize(board) != 7) continue;

                            int handRankIndex = (mapTwoCardSetToIndex(runout) * rangeSize) + rangeIndex;
                            insertSevenCardHandRank(player, board, handRankIndex);
                        }
                    }
                }
            }
            break;

        default:
            assert(false);
            break;
    }
}

int Holdem::getTotalEffectiveStack() const {
    return m_settings.startingPlayerWagers + m_settings.effectiveStackRemaining;
}