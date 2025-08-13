#include <gtest/gtest.h>

#include "game/game_utils.hpp"
#include "game/holdem/parse_input.hpp"
#include "util/result.hpp"

#include <string>

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