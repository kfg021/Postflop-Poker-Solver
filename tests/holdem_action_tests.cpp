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
    Holdem::Settings testSettings;

    void SetUp() override {
        PlayerArray<std::vector<Holdem::RangeElement>> testingRanges = {
            buildRangeFromStrings({"AA", "KJ", "TT", "AQo:50"}).getValue(),
            buildRangeFromStrings({"AA", "KK:25", "QQ", "T9s:33", "27o:99"}).getValue(),
        };

        CardSet testingCommunityCards = buildCommunityCardsFromStrings({ "Ah", "7c", "2s" }).getValue();

        testSettings = {
            .ranges = testingRanges,
            .startingCommunityCards = testingCommunityCards,
            .betSizes = FixedVector<int, holdem::MaxNumBetSizes>{33, 50, 150},
            .raiseSizes = FixedVector<int, holdem::MaxNumRaiseSizes>{50, 100},
            .startingPlayerWagers = 12,
            .effectiveStack = 100,
            .deadMoney = 3
        };
    }
};

enum HoldemActionID : std::uint8_t {
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
} // namespace

TEST_F(HoldemActionTest, CurrentPlayerAlternatesCorrectly) {
    Holdem holdemGame(testSettings);

    GameState state = holdemGame.getInitialGameState();
    EXPECT_EQ(state.playerToAct, Player::P0);

    state = holdemGame.getNewStateAfterDecision(state, Check);
    EXPECT_EQ(state.playerToAct, Player::P1);

    state = holdemGame.getNewStateAfterDecision(state, BetSize0);
    EXPECT_EQ(state.playerToAct, Player::P0);

    state = holdemGame.getNewStateAfterDecision(state, RaiseSize0);
    EXPECT_EQ(state.playerToAct, Player::P1);
}

TEST_F(HoldemActionTest, InitialValidActions) {
    Holdem holdemGame(testSettings);
    auto actions = holdemGame.getValidActions(holdemGame.getInitialGameState());

    EXPECT_EQ(actions.size(), 5);
    EXPECT_TRUE(actions.contains(Check));
    EXPECT_TRUE(actions.contains(BetSize0));
    EXPECT_TRUE(actions.contains(BetSize1));
    EXPECT_TRUE(actions.contains(BetSize2));
    EXPECT_TRUE(actions.contains(AllIn));
}