#include <gtest/gtest.h>

#include "game/game_utils.hpp"
#include "game/holdem/parse_input.hpp"
#include "util/result.hpp"

#include <cmath>
#include <string>

static constexpr float Epsilon = 1e-5;

TEST(CardNameParsingTest, CorrectCardNameParsing) {
    const std::string CardValueNames = "23456789TJQKA";
    const std::string CardSuitNames = "chds";

    for (int i = 0; i < 52; ++i) {
        int valueIndex = i / 4;
        int suitIndex = i % 4;
        std::string cardName = { CardValueNames[valueIndex], CardSuitNames[suitIndex] };
        Result<CardID> cardIDResult = getCardIDFromName(cardName);
        EXPECT_TRUE(cardIDResult.isValue());
        EXPECT_EQ(static_cast<int>(cardIDResult.getValue()), i);
    }
}

TEST(CommunityCardParsingTest, ErrorFromEmptyCommunityCards) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromStrings({});
    EXPECT_TRUE(communityCardResult.isError());
}

TEST(CommunityCardParsingTest, ErrorFromInvalidCardString) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromStrings({ "AB", "12", "AKs" });
    EXPECT_TRUE(communityCardResult.isError());
}

TEST(CommunityCardParsingTest, ErrorFromDuplicateCommunityCards) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromStrings({ "As", "As", "As" });
    EXPECT_TRUE(communityCardResult.isError());
}

TEST(CommunityCardParsingTest, ErrorsFromIncorrectCommunityCardSizes) {
    Result<CardSet> communityCardResult1 = buildCommunityCardsFromStrings({ "2s" });
    EXPECT_TRUE(communityCardResult1.isError());

    Result<CardSet> communityCardResult2 = buildCommunityCardsFromStrings({ "As", "2s", "3s", "4s", "5s", "6s" });
    EXPECT_TRUE(communityCardResult2.isError());
}

TEST(CommunityCardParsingTest, CorrectOutputForFlop) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromStrings({ "As", "7h", "2c" });
    EXPECT_TRUE(communityCardResult.isValue());

    CardSet communityCards = communityCardResult.getValue();
    EXPECT_TRUE(getSetSize(communityCards) == 3);
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("As").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("7h").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("2c").getValue()));
}

TEST(CommunityCardParsingTest, CorrectOutputForTurn) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromStrings({ "Ks", "6h", "Ac", "Jd" });
    EXPECT_TRUE(communityCardResult.isValue());

    CardSet communityCards = communityCardResult.getValue();
    EXPECT_TRUE(getSetSize(communityCards) == 4);
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("Ks").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("6h").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("Ac").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("Jd").getValue()));
}

TEST(CommunityCardParsingTest, CorrectOutputForRiver) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromStrings({ "Qs", "5h", "Kc", "Td", "8s" });
    EXPECT_TRUE(communityCardResult.isValue());

    CardSet communityCards = communityCardResult.getValue();
    EXPECT_TRUE(getSetSize(communityCards) == 5);
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("Qs").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("5h").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("Kc").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("Td").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("8s").getValue()));
}

TEST(RangeParsingTest, ErrorFromEmptyRange) {
    auto rangeResult = buildRangeFromStrings({});
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromInvalidHandString) {
    auto rangeResult = buildRangeFromStrings({ "AB", "12", "Ah" });
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromDuplicatesInRange) {
    auto rangeResult = buildRangeFromStrings({ "AKs", "AKs", "AKs" });
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromDuplicateFlippedHand) {
    auto rangeResult = buildRangeFromStrings({ "AK", "KA" });
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromOverlappingRange) {
    auto rangeResult = buildRangeFromStrings({ "AK:100", "AKs:50", });
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromSmallFrequency) {
    auto rangeResult = buildRangeFromStrings({ "AK:0" });
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromLargeFrequency) {
    auto rangeResult = buildRangeFromStrings({ "AK:101" });
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromSuitedPairHand) {
    auto rangeResult1 = buildRangeFromStrings({ "AAs:50" });
    EXPECT_TRUE(rangeResult1.isError());

    auto rangeResult2 = buildRangeFromStrings({ "AAo:50" });
    EXPECT_TRUE(rangeResult2.isError());
}

TEST(RangeParsingTest, FlippedHandsAreEquivalent) {
    auto rangeResult1 = buildRangeFromStrings({ "A5s:33" });
    EXPECT_TRUE(rangeResult1.isValue());

    auto rangeResult2 = buildRangeFromStrings({ "5As:33" });
    EXPECT_TRUE(rangeResult2.isValue());

    EXPECT_EQ(rangeResult1.getValue(), rangeResult2.getValue());
}

TEST(RangeParsingTest, CorrectNumberOfTotalCombos) {
    auto rangeResult = buildRangeFromStrings({ "JT:50" });
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    EXPECT_EQ(range.hands.size(), 16);
    EXPECT_EQ(range.weights.size(), 16);
}

TEST(RangeParsingTest, CorrectNumberOfSuitedCombos) {
    auto rangeResult = buildRangeFromStrings({ "JTs" });
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    EXPECT_EQ(range.hands.size(), 4);
    EXPECT_EQ(range.weights.size(), 4);
}

TEST(RangeParsingTest, CorrectNumberOfOffsuitCombos) {
    auto rangeResult = buildRangeFromStrings({ "JTo" });
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    EXPECT_EQ(range.hands.size(), 12);
    EXPECT_EQ(range.weights.size(), 12);
}

TEST(RangeParsingTest, CorrectNumberOfPocketPairs) {
    auto rangeResult = buildRangeFromStrings({ "99:33" });
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    EXPECT_EQ(range.hands.size(), 6);
    EXPECT_EQ(range.weights.size(), 6);
}

TEST(RangeParsingTest, NoFrequencyDefaultsTo100) {
    auto rangeResult = buildRangeFromStrings({ "72o" });
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    for (float frequency : range.weights) {
        EXPECT_NEAR(frequency, 1.0f, Epsilon);
    }
}

TEST(RangeParsingTest, CorrectFrequency) {
    auto rangeResult = buildRangeFromStrings({ "72o:67" });
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    for (float frequency : range.weights) {
        EXPECT_NEAR(frequency, 0.67f, Epsilon);
    }
}

TEST(RangeParsingTest, CombineSuitedAndOffsuitCombos) {
    auto rangeResult = buildRangeFromStrings({ "AKs:100", "AKo:50" });
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    EXPECT_EQ(range.hands.size(), 16);
    EXPECT_EQ(range.weights.size(), 16);

    int fullCount = 0;
    int halfCount = 0;
    for (float frequency : range.weights) {
        if (std::abs(frequency - 1.0f) < Epsilon) ++fullCount;
        else if (std::abs(frequency - 0.5f) < Epsilon) ++halfCount;
    }

    EXPECT_EQ(fullCount, 4);
    EXPECT_EQ(halfCount, 12);
}

TEST(RangeParsingTest, CombineAllComboTypes) {
    auto rangeResult = buildRangeFromStrings({ "AKs:100", "AKo:50", "72o", "27s:67", "88", "QJ" });
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    static constexpr int ExpectedSize = 4 + 12 + 12 + 4 + 6 + 16;
    EXPECT_EQ(range.hands.size(), ExpectedSize);
    EXPECT_EQ(range.weights.size(), ExpectedSize);
}

TEST(RangeParsingTest, CorrectRangeFiltering) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromStrings({ "As", "7s", "2s" });
    assert(communityCardResult.isValue());

    CardSet communityCards = communityCardResult.getValue();

    auto rangeResult = buildRangeFromStrings({ "AKs:50", "72o" }, communityCards);
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    
    // 3 combos of AKs, 6 combos of 72o
    static constexpr int ExpectedSize = 9;
    EXPECT_EQ(range.hands.size(), ExpectedSize);
    EXPECT_EQ(range.weights.size(), ExpectedSize);
}

TEST(RangeParsingTest, ErrorFromEmptyRangeAfterFiltering) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromStrings({ "Ac", "Ah", "Ad", "As" });
    assert(communityCardResult.isValue());

    CardSet communityCards = communityCardResult.getValue();

    // No aces left, so AA and AK are impossible
    auto rangeResult = buildRangeFromStrings({ "AKs:50", "AA" }, communityCards);
    EXPECT_TRUE(rangeResult.isError());
}