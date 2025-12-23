#include "game/holdem/holdem.hpp"

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "game/holdem/config.hpp"
#include "game/holdem/hand_evaluation.hpp"
#include "util/fixed_vector.hpp"
#include "util/result.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
enum class Action : std::uint8_t {
    StreetStart,
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

static constexpr CardSet Deck = (1LL << holdem::DeckSize) - 1;

int mapTwoCardSetToIndex(CardSet cardSet) {
    assert(getSetSize(cardSet) == 2);

    int card0Index = static_cast<int>(popLowestCardFromSet(cardSet));
    int card1Index = static_cast<int>(popLowestCardFromSet(cardSet));
    assert(cardSet == 0);

    int finalIndex = card0Index + (card1Index * (card1Index - 1)) / 2;
    assert(finalIndex < holdem::NumPossibleTwoCardHands);
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

Holdem::Holdem(const Settings& settings) : m_settings{ settings } {
    buildHandTables();
}

GameState Holdem::getInitialGameState() const {
    const GameState initialState = {
        .currentBoard = m_settings.startingCommunityCards,
        .totalWagers = { m_settings.startingPlayerWagers, m_settings.startingPlayerWagers },
        .previousStreetsWager = m_settings.startingPlayerWagers,
        .playerToAct = Player::P0,
        .lastAction = static_cast<ActionID>(Action::StreetStart),
        .currentStreet = getStartingStreet()
    };
    return initialState;
}

int Holdem::getDeadMoney() const {
    return m_settings.deadMoney;
}

NodeType Holdem::getNodeType(const GameState& state) const {
    switch (static_cast<Action>(state.lastAction)) {
        case Action::StreetStart:
            if (areBothPlayersAllIn(state)) {
                // Both players are all in, so we need to simulate a runout.
                // We do this by adding chance nodes to the tree until we reach the river.
                if (state.currentStreet == Street::River) {
                    return NodeType::Showdown;
                }
                else {
                    return NodeType::Chance;
                }
            }
            else {
                // Start of street, next player can decide to check / bet
                return NodeType::Decision;
            }

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

            // If we are at the river we are at a showdown node, and if not we are at a chance node
            return (state.currentStreet == Street::River) ? NodeType::Showdown : NodeType::Chance;
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

FixedVector<ActionID, MaxNumActions> Holdem::getValidActions(const GameState& state) const {
    auto addAllValidBetSizes = [this, &state](FixedVector<ActionID, MaxNumActions>& validActions) -> void {
        const auto& currentBetSizes = m_settings.betSizes[state.playerToAct][state.currentStreet];
        for (int i = 0; i < currentBetSizes.size(); ++i) {
            auto newWagersOption = tryGetWagersAfterBet(
                state.totalWagers,
                m_settings.deadMoney,
                state.playerToAct,
                currentBetSizes[i],
                getTotalEffectiveStack()
            );
            if (newWagersOption) {
                ActionID betID = static_cast<ActionID>(Action::BetSize0) + i;
                validActions.pushBack(betID);
            }
        }
    };

    auto addAllValidRaiseSizes = [this, &state](FixedVector<ActionID, MaxNumActions>& validActions) -> void {
        const auto& currentRaiseSizes = m_settings.raiseSizes[state.playerToAct][state.currentStreet];
        for (int i = 0; i < currentRaiseSizes.size(); ++i) {
            auto newWagersOption = tryGetWagersAfterRaise(
                state.totalWagers,
                m_settings.deadMoney,
                state.playerToAct,
                currentRaiseSizes[i],
                getTotalEffectiveStack()
            );
            if (newWagersOption) {
                ActionID raiseID = static_cast<ActionID>(Action::RaiseSize0) + i;
                validActions.pushBack(raiseID);
            }
        }
    };

    NodeType nodeType = getNodeType(state);
    assert(nodeType == NodeType::Decision);

    switch (static_cast<Action>(state.lastAction)) {
        case Action::StreetStart: {
            FixedVector<ActionID, MaxNumActions> validActions = {
                static_cast<ActionID>(Action::Check)
            };
            addAllValidBetSizes(validActions);
            validActions.pushBack(static_cast<ActionID>(Action::AllIn));
            return validActions;
        }

        case Action::Check: {
            // The checking player can only be player 0, because otherwise we would be at a chance node
            assert(getOpposingPlayer(state.playerToAct) == Player::P0);

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
        .previousStreetsWager = state.previousStreetsWager,
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
            const auto& currentBetSizes = m_settings.betSizes[state.playerToAct][state.currentStreet];
            int betIndex = actionID - static_cast<int>(Action::BetSize0);
            assert(betIndex >= 0 && betIndex < currentBetSizes.size());

            auto newWagersOption = tryGetWagersAfterBet(
                state.totalWagers,
                m_settings.deadMoney,
                state.playerToAct,
                currentBetSizes[betIndex],
                getTotalEffectiveStack()
            );
            assert(newWagersOption);

            nextState.totalWagers = *newWagersOption;
            break;
        }

        case Action::RaiseSize0:
        case Action::RaiseSize1:
        case Action::RaiseSize2: {
            const auto& currentRaiseSizes = m_settings.raiseSizes[state.playerToAct][state.currentStreet];
            int raiseIndex = actionID - static_cast<int>(Action::RaiseSize0);
            assert(raiseIndex >= 0 && raiseIndex < currentRaiseSizes.size());

            auto newWagersOption = tryGetWagersAfterRaise(
                state.totalWagers,
                m_settings.deadMoney,
                state.playerToAct,
                currentRaiseSizes[raiseIndex],
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

ChanceNodeInfo Holdem::getChanceNodeInfo(CardSet board) const {
    if (m_settings.useChanceCardIsomorphism) {
        CardSet previouslyDealtCards = board & ~m_settings.startingCommunityCards;
        bool wasTurnCardDealt = (previouslyDealtCards != 0);
        if (wasTurnCardDealt) {
            // The turn is the only street that could be dealt at this point
            // If the river were already dealt, then we wouldn't be at a chance node
            assert(getSetSize(previouslyDealtCards) == 1);
            CardID dealtTurn = getLowestCardInSet(previouslyDealtCards);
            int dealtTurnSuitID = static_cast<int>(getCardSuit(dealtTurn));

            return {
                .availableCards = Deck & ~board,
                .isomorphisms = m_isomorphismsAfterSuitDealt[dealtTurnSuitID]
            };
        }
        else {
            return {
                .availableCards = Deck & ~board,
                .isomorphisms = m_startingIsomorphisms
            };
        }
    }
    else {
        return {
            .availableCards = Deck & ~board,
            .isomorphisms = {}
        };
    }
}

std::span<const CardSet> Holdem::getRangeHands(Player player) const {
    return m_settings.ranges[player].hands;
}

std::span<const float> Holdem::getInitialRangeWeights(Player player) const {
    return m_settings.ranges[player].weights;
}

std::span<const HandData> Holdem::getValidSortedHandRanks(Player player, CardSet board) const {
    assert(getSetSize(board) == 5);

    CardSet chanceCardsDealt = board & ~m_settings.startingCommunityCards;
    int runoutIndex;
    switch (getSetSize(chanceCardsDealt)) {
        case 0:
            assert(getStartingStreet() == Street::River);
            runoutIndex = 0;
            break;
        case 1:
            assert(getStartingStreet() == Street::Turn);
            runoutIndex = static_cast<int>(getLowestCardInSet(chanceCardsDealt));
            break;
        case 2:
            assert(getStartingStreet() == Street::Flop);
            runoutIndex = mapTwoCardSetToIndex(chanceCardsDealt);
            break;
        default:
            assert(false);
            runoutIndex = 0;
            break;
    }

    std::size_t playerRangeSize = m_settings.ranges[player].hands.size();
    std::size_t handRankOffset = runoutIndex * playerRangeSize;

    auto rangeBegin = m_handRanks[player].begin() + handRankOffset;
    auto rangeEnd = rangeBegin + playerRangeSize;

    // Ignore all hands that have rank 0 (overlap with the board)
    while (rangeBegin < rangeEnd && rangeBegin->rank == 0) {
        ++rangeBegin;
    }

    return { rangeBegin, rangeEnd };
}

int Holdem::getHandIndexAfterSuitSwap(Player player, int handIndex, Suit x, Suit y) const {
    assert(m_settings.useChanceCardIsomorphism);

    CardSet swappedHand = swapSuits(m_settings.ranges[player].hands[handIndex], x, y);
    int swappedHandIndexInTable = mapTwoCardSetToIndex(swappedHand);
    int swappedHandIndex = m_handIndices[player][swappedHandIndexInTable];
    assert(swappedHandIndex != -1);

    return swappedHandIndex;
}

std::string Holdem::getActionName(ActionID actionID, int betRaiseSize) const {
    switch (static_cast<Action>(actionID)) {
        case Action::Fold:
            return "Fold";
        case Action::Check:
            return "Check";
        case Action::Call:
            return "Call";
        case Action::BetSize0:
        case Action::BetSize1:
        case Action::BetSize2:
            return "Bet " + std::to_string(betRaiseSize);
        case Action::RaiseSize0:
        case Action::RaiseSize1:
        case Action::RaiseSize2:
            return "Raise " + std::to_string(betRaiseSize);
        case Action::AllIn:
            return "All-in " + std::to_string(betRaiseSize);
        default:
            assert(false);
            return "???";
    }
}

void Holdem::buildHandTables() {
    std::unordered_map<CardSet, HandRank> seenFiveCardHandRanks;

    auto insertSevenCardHandRank = [this, &seenFiveCardHandRanks](Player player, CardSet board, int handRankOffset, int rangeIndex) -> void {
        m_handRanks[player][handRankOffset + rangeIndex] = { .rank = 0, .index = rangeIndex };

        if (getSetSize(board) != 7) return;

        std::array<CardID, 7> sevenCardArray;
        CardSet temp = board;
        for (int i = 0; i < 7; ++i) {
            sevenCardArray[i] = popLowestCardFromSet(temp);
        }
        assert(temp == 0);

        HandRank handRanking = 0;
        for (int i = 0; i < 7; ++i) {
            for (int j = i + 1; j < 7; ++j) {
                CardSet cardsToIgnore = cardIDToSet(sevenCardArray[i]) | cardIDToSet(sevenCardArray[j]);
                CardSet fiveCardHand = board & ~cardsToIgnore;

                HandRank fiveCardHandRanking;
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
        m_handRanks[player][handRankOffset + rangeIndex].rank = handRanking;
    };

    const auto& startingCards = m_settings.startingCommunityCards;
    const auto& ranges = m_settings.ranges;

    switch (getStartingStreet()) {
        case Street::River:
            // We are starting at the river, so we can directly map player range indices into the hand ranking table
            for (Player player : { Player::P0, Player::P1 }) {
                int playerRangeSize = ranges[player].hands.size();
                m_handRanks[player].resize(playerRangeSize);

                for (int rangeIndex = 0; rangeIndex < playerRangeSize; ++rangeIndex) {
                    CardSet board = ranges[player].hands[rangeIndex] | startingCards;
                    insertSevenCardHandRank(player, board, 0, rangeIndex);
                }

                std::sort(m_handRanks[player].begin(), m_handRanks[player].end());
            }
            break;

        case Street::Turn:
            // We are starting at the turn, so we have to consider each possible river runout
            for (Player player : { Player::P0, Player::P1 }) {
                int playerRangeSize = ranges[player].hands.size();
                int handRankTableSize = holdem::DeckSize * playerRangeSize;
                m_handRanks[player].resize(handRankTableSize);

                for (CardID riverCard = 0; riverCard < holdem::DeckSize; ++riverCard) {
                    int handRankOffset = static_cast<int>(riverCard) * playerRangeSize;

                    for (int rangeIndex = 0; rangeIndex < playerRangeSize; ++rangeIndex) {
                        CardSet board = ranges[player].hands[rangeIndex] | startingCards | cardIDToSet(riverCard);
                        insertSevenCardHandRank(player, board, handRankOffset, rangeIndex);
                    }

                    std::sort(
                        m_handRanks[player].begin() + handRankOffset,
                        m_handRanks[player].begin() + handRankOffset + playerRangeSize
                    );
                }
            }
            break;

        case Street::Flop:
            // We are starting at the flop, so we have to consider each possible turn and river runout
            for (Player player : { Player::P0, Player::P1 }) {
                int playerRangeSize = ranges[player].hands.size();
                int handRankTableSize = holdem::NumPossibleTwoCardHands * playerRangeSize;
                m_handRanks[player].resize(handRankTableSize);

                for (CardID turnCard = 0; turnCard < holdem::DeckSize; ++turnCard) {
                    for (CardID riverCard = turnCard + 1; riverCard < holdem::DeckSize; ++riverCard) {
                        CardSet runout = cardIDToSet(turnCard) | cardIDToSet(riverCard);
                        int handRankOffset = mapTwoCardSetToIndex(runout) * playerRangeSize;

                        for (int rangeIndex = 0; rangeIndex < playerRangeSize; ++rangeIndex) {
                            CardSet board = ranges[player].hands[rangeIndex] | startingCards | runout;
                            insertSevenCardHandRank(player, board, handRankOffset, rangeIndex);
                        }

                        std::sort(
                            m_handRanks[player].begin() + handRankOffset,
                            m_handRanks[player].begin() + handRankOffset + playerRangeSize
                        );
                    }
                }
            }
            break;

        default:
            assert(false);
            break;
    }

    // Build hand index table for card isomorphisms
    for (Player player : { Player::P0, Player::P1 }) {
        m_handIndices[player].fill(-1);
        for (int handIndex = 0; handIndex < m_settings.ranges[player].hands.size(); ++handIndex) {
            int handIndexInTable = mapTwoCardSetToIndex(m_settings.ranges[player].hands[handIndex]);
            m_handIndices[player][handIndexInTable] = handIndex;
        }
    }

    // Build chance card isomorphism tables
    if (m_settings.useChanceCardIsomorphism) {
        auto mergeSuitClasses = [](FixedVector<SuitEquivalenceClass, 4>& isomorphisms, Suit x, Suit y) -> void {
            // Inefficient, but there are only 4 suits...
            int suit0Class = -1;
            int suit1Class = -1;
            for (int i = 0; i < isomorphisms.size(); ++i) {
                if (isomorphisms[i].contains(x)) {
                    assert(suit0Class == -1);
                    suit0Class = i;
                }

                if (isomorphisms[i].contains(y)) {
                    assert(suit1Class == -1);
                    suit1Class = i;
                }
            }
            assert(suit0Class != -1 && suit1Class != -1);

            if (suit0Class != suit1Class) {
                FixedVector<SuitEquivalenceClass, 4> newIsomorphisms;

                // The two suits are in different equivalence classes, merge them into one
                SuitEquivalenceClass mergedClass;
                for (Suit suit : isomorphisms[suit0Class]) {
                    assert(!mergedClass.contains(suit));
                    mergedClass.pushBack(suit);
                }
                for (Suit suit : isomorphisms[suit1Class]) {
                    assert(!mergedClass.contains(suit));
                    mergedClass.pushBack(suit);
                }
                newIsomorphisms.pushBack(mergedClass);

                // Add all unchanged isomorphisms to new list
                for (int i = 0; i < isomorphisms.size(); ++i) {
                    if (i != suit0Class && i != suit1Class) {
                        newIsomorphisms.pushBack(isomorphisms[i]);
                    }
                }

                isomorphisms = newIsomorphisms;
            }
        };

        static const FixedVector<SuitEquivalenceClass, 4> IdentityIsomorphism = {
            { Suit::Clubs },
            { Suit::Diamonds },
            { Suit::Hearts },
            { Suit::Spades }
        };

        m_startingIsomorphisms = IdentityIsomorphism;

        bool willTurnBeDealt = (getStartingStreet() == Street::Flop);

        if (willTurnBeDealt) {
            for (int suit = 0; suit < 4; ++suit) {
                m_isomorphismsAfterSuitDealt[suit] = IdentityIsomorphism;
            }
        }

        for (int suit0 = 0; suit0 < 4; ++suit0) {
            for (int suit1 = suit0 + 1; suit1 < 4; ++suit1) {
                // In order for two suits x and y to be isomorphic, we need the three conditions to hold:
                // 1) The values of the starting community cards with suit x must be identical to the values with suit y
                // 2) For all hands in the both player's ranges, the starting weight for that hand needs to be identical to the starting weight for that hand after swapping suits x and y
                // 3) If the turn card was dealt, it cannot be suit x or y (this is checked below for each possible turn suit)

                Suit x = static_cast<Suit>(suit0);
                Suit y = static_cast<Suit>(suit1);

                CardSet suit0Masked = filterCardsWithSuit(m_settings.startingCommunityCards, x);
                CardSet suit1Masked = filterCardsWithSuit(m_settings.startingCommunityCards, y);
                bool isStartingBoardSymmetric = swapSuits(suit0Masked, x, y) == suit1Masked;

                auto areStartingRangesSymmetric = [this, x, y]() -> bool {
                    for (Player player : {Player::P0, Player::P1}) {
                        for (int handIndex = 0; handIndex < m_settings.ranges[player].hands.size(); ++handIndex) {
                            CardSet swappedHand = swapSuits(m_settings.ranges[player].hands[handIndex], x, y);
                            int swappedHandIndexInTable = mapTwoCardSetToIndex(swappedHand);
                            int swappedHandIndex = m_handIndices[player][swappedHandIndexInTable];

                            if (swappedHandIndex == -1) {
                                // Ranges cannot be symmetric, since the swapped hand does not even exist in the player's range
                                return false;
                            }

                            float weightBeforeSwap = m_settings.ranges[player].weights[handIndex];
                            float weightAfterSwap = m_settings.ranges[player].weights[swappedHandIndex];
                            if (weightBeforeSwap != weightAfterSwap) {
                                return false;
                            }
                        }
                    }

                    return true;
                };

                bool areSuitsCombatibleOnStartingBoard = isStartingBoardSymmetric && areStartingRangesSymmetric();

                if (areSuitsCombatibleOnStartingBoard) {
                    mergeSuitClasses(m_startingIsomorphisms, x, y);
                }

                if (willTurnBeDealt) {
                    // This is the third condition that needs to be satisfied for two suits to be isomorphic.
                    // If the turn card was dealt and is either of the suits, then they are not isomorphic.
                    // This is because after the starting board, the ordering of cards matters.
                    // Ex: If the board was Ks2s2h and the turn was Kh, spades and hearts ARE NOT isomorphic because although the cards are the same,
                    // a king on the turn is fundamentally different than a king on the flop. 
                    // However, if the input board was Ks2s2hKh, then spades and hearts ARE isomorphic, because the ordering the starting board doesn't matter
                    // (The input player ranges have already have the information about the ordering of the cards factored in)

                    // We only need to worry about dealt turns, not rivers
                    // This is because after the river is dealt, there are no more cards to deal, so further chance card isomorphism is not possible
                    for (int dealtTurnSuitID = 0; dealtTurnSuitID < 4; ++dealtTurnSuitID) {
                        Suit dealtTurnSuit = static_cast<Suit>(dealtTurnSuitID);
                        bool dealtTurnIsNeitherSuit = (dealtTurnSuit != x) && (dealtTurnSuit != y);

                        if (areSuitsCombatibleOnStartingBoard && dealtTurnIsNeitherSuit) {
                            mergeSuitClasses(m_isomorphismsAfterSuitDealt[dealtTurnSuitID], x, y);
                        }
                    }
                }
            }
        }
    }
}

int Holdem::getTotalEffectiveStack() const {
    return m_settings.startingPlayerWagers + m_settings.effectiveStackRemaining;
}

bool Holdem::areBothPlayersAllIn(const GameState& state) const {
    int totalStack = getTotalEffectiveStack();
    return (state.totalWagers[Player::P0] == totalStack) && (state.totalWagers[Player::P1] == totalStack);
}

Street Holdem::getStartingStreet() const {
    switch (getSetSize(m_settings.startingCommunityCards)) {
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
}