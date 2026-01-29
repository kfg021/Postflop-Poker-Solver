#include <gtest/gtest.h>

#include "game/game_utils.hpp"
#include "game/holdem/holdem_parser.hpp"
#include "util/result.hpp"
#include "util/string_utils.hpp"

#include <cmath>
#include <string>

namespace {
    static constexpr float Epsilon = 1e-5f;
} // namespace

TEST(CardNameParsingTest, CorrectCardNameParsing) {
    const std::string CardValueNames = "23456789TJQKA";
    const std::string CardSuitNames = "cdhs";

    for (int i = 0; i < 52; ++i) {
        int valueIndex = i / 4;
        int suitIndex = i % 4;
        std::string cardName = { CardValueNames[valueIndex], CardSuitNames[suitIndex] };
        Result<CardID> cardIDResult = getCardIDFromName(cardName);
        EXPECT_TRUE(cardIDResult.isValue());
        EXPECT_EQ(static_cast<int>(cardIDResult.getValue()), i);
    }
}

TEST(TokenParsingTest, NoTokensFromEmptyString) {
    EXPECT_TRUE(parseTokens("", ',').empty());
}

TEST(TokenParsingTest, NoTokensFromWhitespace) {
    EXPECT_TRUE(parseTokens(" \t\t\t\t \n\n\n\n    ", ',').empty());
}

TEST(TokenParsingTest, NoTokensFromWhitespaceWithCommas) {
    EXPECT_TRUE(parseTokens(", \t,,,,,,,\t\t\t \n\n,\n\n    , ", ',').empty());
}

TEST(TokenParsingTest, CorrectTokens) {
    std::vector<std::string> tokens = parseTokens("abc, 123, defg, 4567, ", ',');
    EXPECT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0], "abc");
    EXPECT_EQ(tokens[1], "123");
    EXPECT_EQ(tokens[2], "defg");
    EXPECT_EQ(tokens[3], "4567");
}

TEST(TokenParsingTest, CorrectTokensWithWhitesapce) {
    std::vector<std::string> tokens = parseTokens("\n\n\n\n abc, \t\t\t\t123, defg,   4567, \n\n", ',');
    EXPECT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0], "abc");
    EXPECT_EQ(tokens[1], "123");
    EXPECT_EQ(tokens[2], "defg");
    EXPECT_EQ(tokens[3], "4567");
}

TEST(TokenParsingTest, ParseTokenWithSpaces) {
    std::vector<std::string> tokens = parseTokens("abc, def, 1 2 3 4 5 6", ',');
    EXPECT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0], "abc");
    EXPECT_EQ(tokens[1], "def");
    EXPECT_EQ(tokens[2], "1 2 3 4 5 6");
}

TEST(CommunityCardParsingTest, ErrorFromEmptyCommunityCards) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromString("");
    EXPECT_TRUE(communityCardResult.isError());
}

TEST(CommunityCardParsingTest, ErrorFromInvalidCardString) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromString("AB, 12, AKs");
    EXPECT_TRUE(communityCardResult.isError());
}

TEST(CommunityCardParsingTest, ErrorFromDuplicateCommunityCards) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromString("As, As, As");
    EXPECT_TRUE(communityCardResult.isError());
}

TEST(CommunityCardParsingTest, ErrorsFromIncorrectCommunityCardSizes) {
    Result<CardSet> communityCardResult1 = buildCommunityCardsFromString("2s");
    EXPECT_TRUE(communityCardResult1.isError());

    Result<CardSet> communityCardResult2 = buildCommunityCardsFromString("As, 2s, 3s, 4s, 5s, 6s");
    EXPECT_TRUE(communityCardResult2.isError());
}

TEST(CommunityCardParsingTest, CorrectOutputForFlop) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromString("As, 7h, 2c");
    EXPECT_TRUE(communityCardResult.isValue());

    CardSet communityCards = communityCardResult.getValue();
    EXPECT_TRUE(getSetSize(communityCards) == 3);
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("As").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("7h").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("2c").getValue()));
}

TEST(CommunityCardParsingTest, CorrectOutputForTurn) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromString("Ks, 6h, Ac, Jd");
    EXPECT_TRUE(communityCardResult.isValue());

    CardSet communityCards = communityCardResult.getValue();
    EXPECT_TRUE(getSetSize(communityCards) == 4);
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("Ks").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("6h").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("Ac").getValue()));
    EXPECT_TRUE(setContainsCard(communityCards, getCardIDFromName("Jd").getValue()));
}

TEST(CommunityCardParsingTest, CorrectOutputForRiver) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromString("Qs, 5h, Kc, Td, 8s");
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
    auto rangeResult = buildRangeFromString("");
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromInvalidHandString) {
    auto rangeResult = buildRangeFromString("AB, 12, Ah");
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromDuplicatesInRange) {
    auto rangeResult = buildRangeFromString("AKs, AKs, AKs");
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromDuplicateFlippedHand) {
    auto rangeResult = buildRangeFromString("AK, KA");
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromOverlappingRange) {
    auto rangeResult = buildRangeFromString("AK:1.0, AKs:0.5");
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromSmallFrequency) {
    auto rangeResult = buildRangeFromString("AK:0.0");
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromLargeFrequency) {
    auto rangeResult = buildRangeFromString("AK:1.01");
    EXPECT_TRUE(rangeResult.isError());
}

TEST(RangeParsingTest, ErrorFromSuitedPairHand) {
    auto rangeResult1 = buildRangeFromString("AAs:0.5");
    EXPECT_TRUE(rangeResult1.isError());

    auto rangeResult2 = buildRangeFromString("AAo:0.5");
    EXPECT_TRUE(rangeResult2.isError());
}

TEST(RangeParsingTest, FlippedHandsAreEquivalent) {
    auto rangeResult1 = buildRangeFromString("A5s:0.33");
    EXPECT_TRUE(rangeResult1.isValue());

    auto rangeResult2 = buildRangeFromString("5As:0.33");
    EXPECT_TRUE(rangeResult2.isValue());

    EXPECT_EQ(rangeResult1.getValue(), rangeResult2.getValue());
}

TEST(RangeParsingTest, CorrectNumberOfTotalCombos) {
    auto rangeResult = buildRangeFromString("JT:0.5");
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    EXPECT_EQ(range.hands.size(), 16);
    EXPECT_EQ(range.weights.size(), 16);
}

TEST(RangeParsingTest, CorrectNumberOfSuitedCombos) {
    auto rangeResult = buildRangeFromString("JTs");
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    EXPECT_EQ(range.hands.size(), 4);
    EXPECT_EQ(range.weights.size(), 4);
}

TEST(RangeParsingTest, CorrectNumberOfOffsuitCombos) {
    auto rangeResult = buildRangeFromString("JTo");
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    EXPECT_EQ(range.hands.size(), 12);
    EXPECT_EQ(range.weights.size(), 12);
}

TEST(RangeParsingTest, CorrectNumberOfPocketPairs) {
    auto rangeResult = buildRangeFromString("99:0.33");
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    EXPECT_EQ(range.hands.size(), 6);
    EXPECT_EQ(range.weights.size(), 6);
}

TEST(RangeParsingTest, NoFrequencyDefaultsTo100) {
    auto rangeResult = buildRangeFromString("72o");
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    for (float frequency : range.weights) {
        EXPECT_NEAR(frequency, 1.0f, Epsilon);
    }
}

TEST(RangeParsingTest, CorrectFrequency) {
    auto rangeResult = buildRangeFromString("72o:0.12345");
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    for (float frequency : range.weights) {
        EXPECT_NEAR(frequency, 0.12345f, Epsilon);
    }
}

TEST(RangeParsingTest, CombineSuitedAndOffsuitCombos) {
    auto rangeResult = buildRangeFromString("AKs:1.0, AKo:0.5");
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
    auto rangeResult = buildRangeFromString("AKs:1.000, AKo:0.5678, 72o, 27s:0.67, 88, QJ");
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();
    static constexpr int ExpectedSize = 4 + 12 + 12 + 4 + 6 + 16;
    EXPECT_EQ(range.hands.size(), ExpectedSize);
    EXPECT_EQ(range.weights.size(), ExpectedSize);
}

TEST(RangeParsingTest, CorrectRangeFiltering) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromString("As, 7s, 2s");
    assert(communityCardResult.isValue());

    CardSet communityCards = communityCardResult.getValue();

    auto rangeResult = buildRangeFromString("AKs:0.50, 72o", communityCards);
    EXPECT_TRUE(rangeResult.isValue());

    const auto& range = rangeResult.getValue();

    // 3 combos of AKs, 6 combos of 72o
    static constexpr int ExpectedSize = 9;
    EXPECT_EQ(range.hands.size(), ExpectedSize);
    EXPECT_EQ(range.weights.size(), ExpectedSize);
}

TEST(RangeParsingTest, ErrorFromEmptyRangeAfterFiltering) {
    Result<CardSet> communityCardResult = buildCommunityCardsFromString("Ac, Ah, Ad, As");
    assert(communityCardResult.isValue());

    CardSet communityCards = communityCardResult.getValue();

    // No aces left, so AA and AK are impossible
    auto rangeResult = buildRangeFromString("AKs:0.50, AA", communityCards);
    EXPECT_TRUE(rangeResult.isError());
}