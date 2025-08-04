#include "game/holdem/hand_evaluator.hpp"

#include "game/game_types.hpp"
#include "game/game_utils.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>

ShowdownResult HandEvaluator::getShowdownResult(const std::array<CardSet, 2>& playerHands, CardSet board) const {
    std::uint32_t player0Rank = getSevenCardHandRank(playerHands[0] | board);
    std::uint32_t player1Rank = getSevenCardHandRank(playerHands[1] | board);

    if (player0Rank > player1Rank) {
        return ShowdownResult::P0Win;
    }
    else if (player1Rank > player0Rank) {
        return ShowdownResult::P1Win;
    }
    else {
        return ShowdownResult::Tie;
    }
}

std::uint32_t HandEvaluator::getSevenCardHandRank(CardSet sevenCardHand) const {
    assert(getSetSize(sevenCardHand) == 7);

    std::array<CardID, 7> sevenCardArray;
    CardSet temp = sevenCardHand;
    for (int i = 0; i < 7; ++i) {
        CardID lowestCard = getLowestCardInSet(temp);
        sevenCardArray[i] = lowestCard;
        temp &= ~cardIDToSet(lowestCard);
    }
    assert(temp == 0);

    const HandRankTable& HandRank = getHandRankTable();
    std::uint32_t handRanking = 0;
    for (int i = 0; i < 7; ++i) {
        for (int j = i + 1; j < 7; ++j) {
            CardSet cardsToIgnore = cardIDToSet(sevenCardArray[i]) | cardIDToSet(sevenCardArray[j]);
            CardSet fiveCardHand = sevenCardHand & ~cardsToIgnore;
            std::uint32_t handIndex = getFiveCardHandIndex(fiveCardHand);
            assert(handIndex < HandRankTableSize);
            handRanking = std::max(handRanking, HandRank[handIndex]);
        }
    }

    return handRanking;
}

const HandEvaluator::ChooseTable& HandEvaluator::getChooseTable() {
    auto buildChooseTable = []() -> ChooseTable {
        ChooseTable choose;

        for (int n = 0; n < 52; ++n) {
            choose[n][0] = 1;
        }

        for (int k = 1; k < 5; ++k) {
            choose[0][k] = 0;
        }

        for (int n = 1; n < 52; ++n) {
            for (int k = 1; k < 5; ++k) {
                // (n choose k) = (n-1 choose k-1) + (n-1 choose k)
                choose[n][k] = choose[n - 1][k - 1] + choose[n - 1][k];
            }
        }

        return choose;
    };

    static const ChooseTable Choose = buildChooseTable();
    return Choose;
}

const HandEvaluator::HandRankTable& HandEvaluator::getHandRankTable() {
    // Hand rank lookup table is large, so we build it on the heap
    auto buildHandRankTable = []() -> std::unique_ptr<HandRankTable> {
        std::unique_ptr<HandRankTable> handRank = std::make_unique<HandRankTable>();

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

                            std::uint16_t handIndex = getFiveCardHandIndex(hand);
                            assert(handIndex < handRank->size());
                            (*handRank)[handIndex] = getHandRankInternal(hand);
                        }
                    }
                }
            }
        }

        return handRank;
    };

    static const std::unique_ptr<HandRankTable> HandRank = buildHandRankTable();
    return *HandRank;
}

// Maps every possible five card combination to an index from 0 to (52 choose 5)
// https://en.wikipedia.org/wiki/Combinatorial_number_system#Place_of_a_combination_in_the_ordering
std::uint32_t HandEvaluator::getFiveCardHandIndex(CardSet hand) {
    assert(getSetSize(hand) == 5);

    const ChooseTable& Choose = getChooseTable();
    std::uint32_t index = 0;
    for (int i = 0; i < 5; ++i) {
        CardID lowestCard = getLowestCardInSet(hand);
        int n = lowestCard;
        int k = i + 1;
        index += Choose[n][k];
        hand &= ~cardIDToSet(lowestCard);
    }
    assert(hand == 0);

    return index;
}