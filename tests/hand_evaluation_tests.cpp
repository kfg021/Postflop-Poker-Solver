#include <gtest/gtest.h>

#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "game/holdem/hand_evaluation.hpp"

#include <array>
#include <cstdint>
#include <unordered_set>

TEST(HandEvaluationTest, CorrectNumberOfEachHandType) {
    static constexpr int NumHandRankings = 10;
    std::array<std::uint32_t, NumHandRankings> ExpectedTotalHandsPerRank = {
        1302540,    // High Card
        1098240,    // One Pair
        123552,     // Two Pair
        54912,      // Three of a Kind
        10200,      // Straight
        5108,       // Flush
        3744,       // Full House
        624,        // Four of a Kind
        36,         // Straight Flush
        4,          // Royal Flush
    };

    hand_evaluation::buildLookupTablesIfNeeded();

    std::array<std::uint32_t, NumHandRankings> totalHandsPerRank = {};
    for (CardID card0 = 0; card0 < 52; ++card0) {
        for (CardID card1 = card0 + 1; card1 < 52; ++card1) {
            for (CardID card2 = card1 + 1; card2 < 52; ++card2) {
                for (CardID card3 = card2 + 1; card3 < 52; ++card3) {
                    for (CardID card4 = card3 + 1; card4 < 52; ++card4) {
                        CardSet hand = cardIDToSet(card0) |
                            cardIDToSet(card1) |
                            cardIDToSet(card2) |
                            cardIDToSet(card3) |
                            cardIDToSet(card4);

                        std::uint32_t handRank = hand_evaluation::getFiveCardHandRank(hand);
                        std::uint8_t handType = (handRank >> 20) & 0xF;
                        EXPECT_LT(handType, NumHandRankings);
                        ++totalHandsPerRank[handType];
                    }
                }
            }
        }
    }

    EXPECT_EQ(totalHandsPerRank, ExpectedTotalHandsPerRank);
}

TEST(HandEvaluationTest, CorrectNumberOfIsomorphicHands) {
    static constexpr int ExpectedNumIsomorphicHands = 7462;
    std::unordered_set<std::uint32_t> handIsomorphisms;

    hand_evaluation::buildLookupTablesIfNeeded();

    for (CardID card0 = 0; card0 < 52; ++card0) {
        for (CardID card1 = card0 + 1; card1 < 52; ++card1) {
            for (CardID card2 = card1 + 1; card2 < 52; ++card2) {
                for (CardID card3 = card2 + 1; card3 < 52; ++card3) {
                    for (CardID card4 = card3 + 1; card4 < 52; ++card4) {
                        CardSet hand = cardIDToSet(card0) |
                            cardIDToSet(card1) |
                            cardIDToSet(card2) |
                            cardIDToSet(card3) |
                            cardIDToSet(card4);

                        std::uint32_t handRank = hand_evaluation::getFiveCardHandRank(hand);
                        handIsomorphisms.insert(handRank);
                    }
                }
            }
        }
    }

    int numIsomorphicHands = handIsomorphisms.size();
    EXPECT_EQ(numIsomorphicHands, ExpectedNumIsomorphicHands);
}