#include <gtest/gtest.h>

#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "game/holdem/config.hpp"
#include "game/holdem/holdem_parser.hpp"
#include "game/holdem/holdem.hpp"
#include "util/fixed_vector.hpp"

namespace {
class HoldemIsomorphismTest : public ::testing::Test {
protected:
    static inline Holdem::Settings testSettings;
    static constexpr int ExpectedNumAvailableTurnCards = holdem::DeckSize - 3;
    static constexpr int ExpectedNumAvailableRiverCards = holdem::DeckSize - 4;

    static void SetUpTestSuite() {
        PlayerArray<Holdem::Range> testingRanges = {
            buildRangeFromString("AA, KJ, TT, AQo:0.50").getValue(),
            buildRangeFromString("AA, KK:0.25, QQ, T9s:0.33, 27o:0.99").getValue(),
        };

        FixedVector<int, holdem::MaxNumBetSizes> betSizes{ 33, 100, 150 };
        FixedVector<int, holdem::MaxNumRaiseSizes> raiseSizes{ 50, 100 };

        testSettings = {
            .ranges = testingRanges,
            .startingCommunityCards = 0, // Each test uses its own set of community cards
            .betSizes = { { betSizes, betSizes, betSizes },  { betSizes, betSizes, betSizes } },
            .raiseSizes = { { raiseSizes, raiseSizes, raiseSizes },  { raiseSizes, raiseSizes, raiseSizes } },
            .startingPlayerWagers = 12,
            .effectiveStackRemaining = 360,
            .deadMoney = 3,
            .useChanceCardIsomorphism = true
        };
    }
};

int getNumberOfNontrivialEquivalences(const FixedVector<SuitEquivalenceClass, 4>& isomorphisms) {
    int count = 0;
    for (const SuitEquivalenceClass& isomorphism : isomorphisms) {
        count += (isomorphism.size() > 1);
    }
    return count;
}

bool containsEquivalence(const FixedVector<SuitEquivalenceClass, 4>& isomorphisms, SuitEquivalenceClass equivalence) {
    for (const SuitEquivalenceClass& isomorphism : isomorphisms) {
        if (isomorphism.size() != equivalence.size()) continue;

        bool isEquivalent = true;
        for (Suit suit : isomorphism) {
            isEquivalent &= equivalence.contains(suit);
        }

        if (isEquivalent) {
            return true;
        }
    }

    return false;
}
} // namespace

// TODO: Add tests for uneven porportions of offsuit starting hand combos
TEST_F(HoldemIsomorphismTest, NoIsomorphismsOnRainbowFlop) {
    Holdem::Settings customSettings = testSettings;
    customSettings.startingCommunityCards = buildCommunityCardsFromString("Ah, 7c, 2s").getValue();
    Holdem holdemRules{ customSettings };

    ChanceNodeInfo turnCardInfo = holdemRules.getChanceNodeInfo(customSettings.startingCommunityCards);
    ASSERT_EQ(getSetSize(turnCardInfo.availableCards), ExpectedNumAvailableTurnCards);
    ASSERT_EQ(getNumberOfNontrivialEquivalences(turnCardInfo.isomorphisms), 0);

    CardSet turn = cardIDToSet(getCardIDFromValueAndSuit(Value::Three, Suit::Diamonds));
    ChanceNodeInfo riverCardInfo = holdemRules.getChanceNodeInfo(customSettings.startingCommunityCards | turn);
    ASSERT_EQ(getSetSize(riverCardInfo.availableCards), ExpectedNumAvailableRiverCards);
    ASSERT_EQ(getNumberOfNontrivialEquivalences(riverCardInfo.isomorphisms), 0);
}

TEST_F(HoldemIsomorphismTest, NoIsomorphismsOnRainbowTurn) {
    Holdem::Settings customSettings = testSettings;
    customSettings.startingCommunityCards = buildCommunityCardsFromString("Ah, 7c, 2s, 3d").getValue();
    Holdem holdemRules{ customSettings };

    ChanceNodeInfo riverCardInfo = holdemRules.getChanceNodeInfo(customSettings.startingCommunityCards);
    ASSERT_EQ(getSetSize(riverCardInfo.availableCards), ExpectedNumAvailableRiverCards);
    ASSERT_EQ(getNumberOfNontrivialEquivalences(riverCardInfo.isomorphisms), 0);
}

TEST_F(HoldemIsomorphismTest, OneIsomorphismOnTwoToneFlop) {
    Holdem::Settings customSettings = testSettings;
    customSettings.startingCommunityCards = buildCommunityCardsFromString("Ah, 7c, 2c").getValue();
    Holdem holdemRules{ customSettings };

    // Diamonds and spades don't appear, so they are isomorphic
    ChanceNodeInfo turnCardInfo = holdemRules.getChanceNodeInfo(customSettings.startingCommunityCards);
    ASSERT_EQ(getSetSize(turnCardInfo.availableCards), ExpectedNumAvailableTurnCards);
    ASSERT_EQ(getNumberOfNontrivialEquivalences(turnCardInfo.isomorphisms), 1);
    ASSERT_TRUE(containsEquivalence(turnCardInfo.isomorphisms, { Suit::Diamonds, Suit::Spades }));

    // After a club or heart turn, isomorphism remains
    CardSet turnClub = cardIDToSet(getCardIDFromValueAndSuit(Value::Ten, Suit::Clubs));
    ChanceNodeInfo riverCardInfoClub = holdemRules.getChanceNodeInfo(customSettings.startingCommunityCards | turnClub);
    ASSERT_EQ(getSetSize(riverCardInfoClub.availableCards), ExpectedNumAvailableRiverCards);
    ASSERT_EQ(getNumberOfNontrivialEquivalences(riverCardInfoClub.isomorphisms), 1);
    ASSERT_TRUE(containsEquivalence(riverCardInfoClub.isomorphisms, { Suit::Diamonds, Suit::Spades }));

    // After a diamond or spade turn, the isomorphism is broken
    CardSet turnDiamond = cardIDToSet(getCardIDFromValueAndSuit(Value::Three, Suit::Diamonds));
    ChanceNodeInfo riverCardInfoDiamond = holdemRules.getChanceNodeInfo(customSettings.startingCommunityCards | turnDiamond);
    ASSERT_EQ(getSetSize(riverCardInfoDiamond.availableCards), ExpectedNumAvailableRiverCards);
    ASSERT_EQ(getNumberOfNontrivialEquivalences(riverCardInfoDiamond.isomorphisms), 0);
}

TEST_F(HoldemIsomorphismTest, OneIsomorphismOnMonotoneFlop) {
    Holdem::Settings customSettings = testSettings;
    customSettings.startingCommunityCards = buildCommunityCardsFromString("Ah, 7h, 2h").getValue();
    Holdem holdemRules{ customSettings };

    // All suits except hearts are isomorphic
    ChanceNodeInfo turnCardInfo = holdemRules.getChanceNodeInfo(customSettings.startingCommunityCards);
    ASSERT_EQ(getSetSize(turnCardInfo.availableCards), ExpectedNumAvailableTurnCards);
    ASSERT_EQ(getNumberOfNontrivialEquivalences(turnCardInfo.isomorphisms), 1);
    ASSERT_TRUE(containsEquivalence(turnCardInfo.isomorphisms, { Suit::Diamonds, Suit::Clubs, Suit::Spades }));

    // After a heart turn, isomorphism remains
    CardSet turnHeart = cardIDToSet(getCardIDFromValueAndSuit(Value::Ten, Suit::Hearts));
    ChanceNodeInfo riverCardInfoSpade = holdemRules.getChanceNodeInfo(customSettings.startingCommunityCards | turnHeart);
    ASSERT_EQ(getSetSize(riverCardInfoSpade.availableCards), ExpectedNumAvailableRiverCards);
    ASSERT_EQ(getNumberOfNontrivialEquivalences(riverCardInfoSpade.isomorphisms), 1);
    ASSERT_TRUE(containsEquivalence(riverCardInfoSpade.isomorphisms, { Suit::Diamonds, Suit::Clubs, Suit::Spades }));

    // After a diamond turn card, diamond is no longer part of the isomorphism
    CardSet turnDiamond = cardIDToSet(getCardIDFromValueAndSuit(Value::Three, Suit::Diamonds));
    ChanceNodeInfo riverCardInfoDiamond = holdemRules.getChanceNodeInfo(customSettings.startingCommunityCards | turnDiamond);
    ASSERT_EQ(getSetSize(riverCardInfoDiamond.availableCards), ExpectedNumAvailableRiverCards);
    ASSERT_EQ(getNumberOfNontrivialEquivalences(riverCardInfoDiamond.isomorphisms), 1);
    ASSERT_TRUE(containsEquivalence(riverCardInfoDiamond.isomorphisms, { Suit::Clubs, Suit::Spades }));
}

TEST_F(HoldemIsomorphismTest, TwoIsomorphismsOnDoublePairedTurn) {
    Holdem::Settings customSettings = testSettings;
    customSettings.startingCommunityCards = buildCommunityCardsFromString("Ks, 2s, 2h, Kh").getValue();
    Holdem holdemRules{ customSettings };

    // There are two sets of isomorphisms on this board
    ChanceNodeInfo riverCardInfo = holdemRules.getChanceNodeInfo(customSettings.startingCommunityCards);
    ASSERT_EQ(getSetSize(riverCardInfo.availableCards), ExpectedNumAvailableRiverCards);
    ASSERT_EQ(getNumberOfNontrivialEquivalences(riverCardInfo.isomorphisms), 2);
    ASSERT_TRUE(containsEquivalence(riverCardInfo.isomorphisms, { Suit::Spades, Suit::Hearts }));
    ASSERT_TRUE(containsEquivalence(riverCardInfo.isomorphisms, { Suit::Clubs, Suit::Diamonds }));
}

TEST_F(HoldemIsomorphismTest, OneIsomorphismOnDoublePairedDealtTurn) {
    Holdem::Settings customSettings = testSettings;
    customSettings.startingCommunityCards = buildCommunityCardsFromString("Ks, 2s, 2h").getValue();
    Holdem holdemRules{ customSettings };

    // The last king was dealt, so spades and hearts are not isomorphic
    CardSet turn = cardIDToSet(getCardIDFromValueAndSuit(Value::King, Suit::Hearts));
    ChanceNodeInfo riverCardInfo = holdemRules.getChanceNodeInfo(customSettings.startingCommunityCards | turn);
    ASSERT_EQ(getSetSize(riverCardInfo.availableCards), ExpectedNumAvailableRiverCards);
    ASSERT_EQ(getNumberOfNontrivialEquivalences(riverCardInfo.isomorphisms), 1);
    ASSERT_TRUE(containsEquivalence(riverCardInfo.isomorphisms, { Suit::Clubs, Suit::Diamonds }));
}