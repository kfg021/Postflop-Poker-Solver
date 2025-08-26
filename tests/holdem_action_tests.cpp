#include <gtest/gtest.h>

#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "game/holdem/config.hpp"
#include "game/holdem/holdem.hpp"
#include "game/holdem/parse_input.hpp"
#include "util/fixed_vector.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {
class HoldemActionTest : public ::testing::Test {
protected:
    static inline Holdem::Settings testSettings;

    static void SetUpTestSuite() {
        PlayerArray<Holdem::Range> testingRanges = {
            buildRangeFromStrings({"AA", "KJ", "TT", "AQo:50"}).getValue(),
            buildRangeFromStrings({"AA", "KK:25", "QQ", "T9s:33", "27o:99"}).getValue(),
        };

        CardSet testingCommunityCards = buildCommunityCardsFromStrings({ "Ah", "7c", "2s" }).getValue();

        testSettings = {
            .ranges = testingRanges,
            .startingCommunityCards = testingCommunityCards,
            .betSizes = FixedVector<int, holdem::MaxNumBetSizes>{33, 100, 150},
            .raiseSizes = FixedVector<int, holdem::MaxNumRaiseSizes>{50, 100},
            .startingPlayerWagers = 12,
            .effectiveStackRemaining = 360,
            .deadMoney = 3
        };
    }
};

enum HoldemActionID : std::uint8_t {
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

GameState simulateStreet(const Holdem& holdemRules, Street street, const std::vector<HoldemActionID>& actions) {
    auto simulateFromState = [&holdemRules](const GameState& state, const std::vector<HoldemActionID>& actions) -> GameState {
        GameState newState = state;
        for (HoldemActionID actionID : actions) {
            newState = holdemRules.getNewStateAfterDecision(newState, actionID);
        }
        return newState;
    };

    auto getStateAfterChance = [&holdemRules](const GameState& state) -> GameState {
        return holdemRules.getNewStatesAfterChance(state)[0];
    };

    auto getStateAtStartOfStreet = [&holdemRules, &simulateFromState, &getStateAfterChance](Street street) {
        if (street == Street::Flop) {
            return holdemRules.getInitialGameState();
        }
        else if (street == Street::Turn) {
            GameState endFlopState = simulateFromState(holdemRules.getInitialGameState(), { Check, Check });
            return getStateAfterChance(endFlopState);
        }
        else {
            GameState endFlopState = simulateFromState(holdemRules.getInitialGameState(), { Check, Check });
            GameState endTurnState = simulateFromState(getStateAfterChance(endFlopState), { Check, Check });
            return getStateAfterChance(endTurnState);
        }
    };

    return simulateFromState(getStateAtStartOfStreet(street), actions);
}
} // namespace

TEST_F(HoldemActionTest, CurrentPlayerAlternatesCorrectly) {
    Holdem holdemRules{ testSettings };

    GameState state = holdemRules.getInitialGameState();
    EXPECT_EQ(state.playerToAct, Player::P0);

    state = holdemRules.getNewStateAfterDecision(state, Check);
    EXPECT_EQ(state.playerToAct, Player::P1);

    state = holdemRules.getNewStateAfterDecision(state, BetSize0);
    EXPECT_EQ(state.playerToAct, Player::P0);

    state = holdemRules.getNewStateAfterDecision(state, RaiseSize0);
    EXPECT_EQ(state.playerToAct, Player::P1);
}

TEST_F(HoldemActionTest, InitialValidActions) {
    Holdem holdemRules{ testSettings };
    auto actions = holdemRules.getValidActions(holdemRules.getInitialGameState());

    EXPECT_EQ(actions.size(), 5);
    EXPECT_TRUE(actions.contains(Check));
    EXPECT_TRUE(actions.contains(BetSize0));
    EXPECT_TRUE(actions.contains(BetSize1));
    EXPECT_TRUE(actions.contains(BetSize2));
    EXPECT_TRUE(actions.contains(AllIn));
}

TEST_F(HoldemActionTest, ValidActionsAfterCheck) {
    Holdem holdemRules{ testSettings };
    GameState state = simulateStreet(holdemRules, Street::Flop, { Check });
    auto actions = holdemRules.getValidActions(state);

    EXPECT_EQ(actions.size(), 5);
    EXPECT_TRUE(actions.contains(Check));
    EXPECT_TRUE(actions.contains(BetSize0));
    EXPECT_TRUE(actions.contains(BetSize1));
    EXPECT_TRUE(actions.contains(BetSize2));
    EXPECT_TRUE(actions.contains(AllIn));
}

TEST_F(HoldemActionTest, ValidActionsAfterBet) {
    Holdem holdemRules{ testSettings };
    GameState state = simulateStreet(holdemRules, Street::Flop, { BetSize0 });
    auto actions = holdemRules.getValidActions(state);

    EXPECT_EQ(actions.size(), 5);
    EXPECT_TRUE(actions.contains(Fold));
    EXPECT_TRUE(actions.contains(Call));
    EXPECT_TRUE(actions.contains(RaiseSize0));
    EXPECT_TRUE(actions.contains(RaiseSize1));
    EXPECT_TRUE(actions.contains(AllIn));
}

TEST_F(HoldemActionTest, FoldIsTerminal) {
    Holdem holdemRules{ testSettings };
    GameState state = simulateStreet(holdemRules, Street::Flop, { Check, Fold });
    EXPECT_EQ(holdemRules.getNodeType(state), NodeType::Fold);

    // No additional wagers, wagers should be equal to start
    constexpr PlayerArray<int> ExpectedWagers = { 12, 12 };
    EXPECT_EQ(state.totalWagers, ExpectedWagers);
}

TEST_F(HoldemActionTest, EndOfFlopAndTurnIsChance) {
    Holdem holdemRules{ testSettings };

    GameState endFlopState = simulateStreet(holdemRules, Street::Flop, { Check, Check });
    EXPECT_EQ(holdemRules.getNodeType(endFlopState), NodeType::Chance);

    GameState endTurnState = simulateStreet(holdemRules, Street::Turn, { Check, BetSize1, RaiseSize0, Call });
    EXPECT_EQ(holdemRules.getNodeType(endTurnState), NodeType::Chance);
}

TEST_F(HoldemActionTest, EndOfRiverIsShowdown) {
    Holdem holdemRules{ testSettings };
    GameState state = simulateStreet(holdemRules, Street::River, { Check, BetSize1, Call });
    EXPECT_EQ(holdemRules.getNodeType(state), NodeType::Showdown);

    // On the river player 0 bets pot, which is an additional 12 + 12 + 3 = 27
    // So total wager is 12 + 27 = 39
    constexpr PlayerArray<int> ExpectedWagers = { 39, 39 };
    EXPECT_EQ(state.totalWagers, ExpectedWagers);
}

TEST_F(HoldemActionTest, RiverAllInIsShowdown) {
    Holdem holdemRules{ testSettings };
    GameState state = simulateStreet(holdemRules, Street::River, { Check, BetSize1, AllIn, Call });
    EXPECT_EQ(holdemRules.getNodeType(state), NodeType::Showdown);
}

TEST_F(HoldemActionTest, TurnAllInIsChanceShowdown) {
    Holdem holdemRules{ testSettings };
    GameState state = simulateStreet(holdemRules, Street::Turn, { Check, BetSize1, AllIn, Call });
    EXPECT_EQ(holdemRules.getNodeType(state), NodeType::Chance);

    for (GameState stateAfterChance : holdemRules.getNewStatesAfterChance(state)) {
        EXPECT_EQ(holdemRules.getNodeType(stateAfterChance), NodeType::Showdown);
    }
}

TEST_F(HoldemActionTest, FlopAllInIsChanceChanceShowdown) {
    Holdem holdemRules{ testSettings };
    GameState state = simulateStreet(holdemRules, Street::Flop, { Check, BetSize1, AllIn, Call });
    EXPECT_EQ(holdemRules.getNodeType(state), NodeType::Chance);

    for (GameState stateAfterFirstChance : holdemRules.getNewStatesAfterChance(state)) {
        EXPECT_EQ(holdemRules.getNodeType(stateAfterFirstChance), NodeType::Chance);
        for (GameState stateAfterSecondChance : holdemRules.getNewStatesAfterChance(stateAfterFirstChance)) {
            EXPECT_EQ(holdemRules.getNodeType(stateAfterSecondChance), NodeType::Showdown);
        }
    }
}

TEST_F(HoldemActionTest, CorrectNumberOfChanceNodes) {
    Holdem holdemRules{ testSettings };

    GameState endFlopState = simulateStreet(holdemRules, Street::Flop, { Check, Check });
    EXPECT_EQ(holdemRules.getNodeType(endFlopState), NodeType::Chance);

    auto endFlopChanceNodes = holdemRules.getNewStatesAfterChance(endFlopState);
    EXPECT_EQ(endFlopChanceNodes.size(), holdem::DeckSize - 3);

    GameState endTurnState = simulateStreet(holdemRules, Street::Turn, { Check, Check });
    EXPECT_EQ(holdemRules.getNodeType(endTurnState), NodeType::Chance);

    auto endTurnChanceNodes = holdemRules.getNewStatesAfterChance(endTurnState);
    EXPECT_EQ(endTurnChanceNodes.size(), holdem::DeckSize - 4);
}

TEST_F(HoldemActionTest, BetSizesRoundUp) {
    Holdem holdemRules{ testSettings };

    // Pot: 27
    // 33% bet is 8.91, rounds up to 9
    // So after a bet player 0 should be wagering 12 + 9 = 21
    GameState state = holdemRules.getNewStateAfterDecision(holdemRules.getInitialGameState(), BetSize0);
    constexpr PlayerArray<int> ExpectedWagersAfterBet = { 21, 12 };
    EXPECT_EQ(state.totalWagers, ExpectedWagersAfterBet);

    // Raise: 50%
    // After matching the bet, the pot is 21 + 21 + 3 = 45
    // 50% raise is 22.5, rounds up to 23, so total player 1 wager is 21 + 23 = 44
    state = holdemRules.getNewStateAfterDecision(state, RaiseSize0);
    constexpr PlayerArray<int> ExpectedWagersAfterBetRaise = { 21, 44 };
    EXPECT_EQ(state.totalWagers, ExpectedWagersAfterBetRaise);

    // Call: Match current bet
    state = holdemRules.getNewStateAfterDecision(state, Call);
    constexpr PlayerArray<int> ExpectedWagersAfterBetRaiseCall = { 44, 44 };
    EXPECT_EQ(state.totalWagers, ExpectedWagersAfterBetRaiseCall);
}

TEST_F(HoldemActionTest, CorrectRaiseSizes) {
    Holdem holdemRules{ testSettings };

    // Pot: 27 Bet: 150% (Rounds to 41)
    // So after a bet player 0 should be wagering 12 + 41 = 53
    GameState state = holdemRules.getNewStateAfterDecision(holdemRules.getInitialGameState(), BetSize2);
    constexpr PlayerArray<int> ExpectedWagersAfterBet = { 53, 12 };
    EXPECT_EQ(state.totalWagers, ExpectedWagersAfterBet);

    // The two defined raise sizes should be valid 
    auto actions = holdemRules.getValidActions(state);
    EXPECT_TRUE(actions.contains(RaiseSize0));
    EXPECT_TRUE(actions.contains(RaiseSize1));
    EXPECT_FALSE(actions.contains(RaiseSize2));

    // Raise: 50%
    // After matching the bet, the pot is 53 + 53 + 3 = 109
    // 50% raise is 54.5, rounds up to 55, so total player 1 wager is 53 + 55 = 108
    state = holdemRules.getNewStateAfterDecision(state, RaiseSize0);
    constexpr PlayerArray<int> ExpectedWagersAfterBetRaise = { 53, 108 };
    EXPECT_EQ(state.totalWagers, ExpectedWagersAfterBetRaise);

    // The two defined raise sizes should still be valid 
    actions = holdemRules.getValidActions(state);
    EXPECT_TRUE(actions.contains(RaiseSize0));
    EXPECT_TRUE(actions.contains(RaiseSize1));
    EXPECT_FALSE(actions.contains(RaiseSize2));

    // Raise: 100%
    // After matching the bet, the pot is 108 + 108 + 3 = 219
    // 100% raise is 219, so total player 0 wager is 108 + 219 = 327
    state = holdemRules.getNewStateAfterDecision(state, RaiseSize1);
    constexpr PlayerArray<int> ExpectedWagersAfterBetRaiseRaise = { 327, 108 };
    EXPECT_EQ(state.totalWagers, ExpectedWagersAfterBetRaiseRaise);

    // Pot is too large for any more raises (effective stack is 360)
    actions = holdemRules.getValidActions(state);
    EXPECT_EQ(actions.size(), 3);
    EXPECT_TRUE(actions.contains(Fold));
    EXPECT_TRUE(actions.contains(Call));
    EXPECT_TRUE(actions.contains(AllIn));
}

TEST_F(HoldemActionTest, IgnoreAllInWhenSameAsCall) {
    Holdem holdemRules{ testSettings };
    auto actions = holdemRules.getValidActions(simulateStreet(holdemRules, Street::Flop, { AllIn }));

    EXPECT_FALSE(actions.contains(AllIn));

    EXPECT_TRUE(actions.size() == 2);
    EXPECT_TRUE(actions.contains(Fold));
    EXPECT_TRUE(actions.contains(Call));
}

TEST_F(HoldemActionTest, IgnoreBetWhenSameAsAllIn) {
    Holdem::Settings customTestSettings = testSettings;
    customTestSettings.startingPlayerWagers = 6;
    customTestSettings.effectiveStackRemaining = 4;
    customTestSettings.deadMoney = 0;

    Holdem holdemRules{ customTestSettings };
    auto actions = holdemRules.getValidActions(holdemRules.getInitialGameState());

    // Pot is 6 + 6 + 0 = 12
    // 33% Bet would be betting 4, which is same action as all in, so we don't allow it
    EXPECT_TRUE(actions.size() == 2);
    EXPECT_TRUE(actions.contains(Check));
    EXPECT_TRUE(actions.contains(AllIn));
}

TEST_F(HoldemActionTest, IgnoreRaiseWhenSameAsAllIn) {
    Holdem::Settings customTestSettings = testSettings;
    customTestSettings.startingPlayerWagers = 6;
    customTestSettings.effectiveStackRemaining = 14;
    customTestSettings.deadMoney = 0;

    Holdem holdemRules{ customTestSettings };
    auto actions = holdemRules.getValidActions(simulateStreet(holdemRules, Street::Flop, { Check, BetSize0 }));

    // After the first bet, the wagers are [6, 10]
    // 50% raise from player 0 would first match player 1's bet -> wagers become [10, 10], then raise 10 more
    // So overall wager from player 0 is 20, which is equivalent to an all in, so we dont allow it
    EXPECT_TRUE(actions.size() == 3);
    EXPECT_TRUE(actions.contains(Fold));
    EXPECT_TRUE(actions.contains(Call));
    EXPECT_TRUE(actions.contains(AllIn));
}