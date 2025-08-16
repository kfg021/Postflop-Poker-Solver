#include <gtest/gtest.h>

#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "game/holdem/hand_evaluation.hpp"

#include <array>
#include <cstdint>
#include <unordered_set>

namespace {
class HandEvaluationTest : public ::testing::Test {
protected:
    static inline std::vector<std::uint32_t> handRanks;

    static void SetUpTestSuite() {
        handRanks.reserve(2598960); // 52 choose 5

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

                            handRanks.push_back(getFiveCardHandRank(hand));
                        }
                    }
                }
            }
        }
    }
};
} // namespace

TEST_F(HandEvaluationTest, AllRankingsAreNonZero) {
    for (std::uint32_t handRank : handRanks) {
        EXPECT_NE(handRank, 0);
    }
}

TEST_F(HandEvaluationTest, CorrectNumberOfEachHandType) {
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

    std::array<std::uint32_t, NumHandRankings> totalHandsPerRank = {};
    for (std::uint32_t handRank : handRanks) {
        std::uint8_t handType = ((handRank >> 20) & 0xF) - 1;
        EXPECT_LT(handType, NumHandRankings);
        ++totalHandsPerRank[handType];
    }

    EXPECT_EQ(totalHandsPerRank, ExpectedTotalHandsPerRank);
}

TEST_F(HandEvaluationTest, CorrectNumberOfIsomorphicHands) {
    static constexpr int ExpectedNumIsomorphicHands = 7462;
    std::unordered_set<std::uint32_t> handIsomorphisms(handRanks.begin(), handRanks.end());
    int numIsomorphicHands = handIsomorphisms.size();
    EXPECT_EQ(numIsomorphicHands, ExpectedNumIsomorphicHands);
}