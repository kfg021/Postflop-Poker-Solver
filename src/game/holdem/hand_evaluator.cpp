#include "game/holdem/hand_evaluator.hpp"

#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "util/fixed_vector.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>

HandEvaluator::HandEvaluator() {
    // Ensure lookup tables are created
    static_cast<void>(getChooseTable());
    static_cast<void>(getHandRankTable());
}

std::uint32_t HandEvaluator::getFiveCardHandRank(CardSet hand) const {
    assert(getSetSize(hand) == 5);

    const HandRankTable& HandRank = getHandRankTable();
    std::uint32_t handIndex = getFiveCardHandIndex(hand);
    assert(handIndex < HandRankTableSize);
    return HandRank[handIndex];
}

std::uint32_t HandEvaluator::getSevenCardHandRank(CardSet hand) const {
    assert(getSetSize(hand) == 7);

    std::array<CardID, 7> sevenCardArray;
    CardSet temp = hand;
    for (int i = 0; i < 7; ++i) {
        CardID lowestCard = getLowestCardInSet(temp);
        sevenCardArray[i] = lowestCard;
        temp &= ~cardIDToSet(lowestCard);
    }
    assert(temp == 0);

    std::uint32_t handRanking = 0;
    for (int i = 0; i < 7; ++i) {
        for (int j = i + 1; j < 7; ++j) {
            CardSet cardsToIgnore = cardIDToSet(sevenCardArray[i]) | cardIDToSet(sevenCardArray[j]);
            CardSet fiveCardHand = hand & ~cardsToIgnore;
            handRanking = std::max(handRanking, getFiveCardHandRank(fiveCardHand));
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
                            (*handRank)[handIndex] = generateFiveCardHandRank(hand);
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

std::uint32_t HandEvaluator::generateFiveCardHandRank(CardSet hand) {
    enum class HandType : std::uint8_t {
        HighCard,
        Pair,
        TwoPair,
        ThreeOfAKind,
        Straight,
        Flush,
        FullHouse,
        FourOfAKind,
        StraightFlush,
        RoyalFlush,
    };

    struct HandRank {
        HandType handType;
        FixedVector<Value, 5> kickers;
    };

    auto convertHandRankToInt = [](const HandRank& handRank) -> std::uint32_t {
        // Integer representation;
        // Bits [23, 20]: Hand type
        // Bits [19, 16]: Kicker 0
        // Bits [15, 12]: Kicker 1
        // Bits [11, 8]:  Kicker 2
        // Bits [7, 4]:   Kicker 3
        // Bits [3, 0]:   Kicker 4
        // Any bits with non existient kickers are set to 0

        std::uint32_t handRankID = 0;

        std::uint8_t handTypeID = static_cast<std::uint8_t>(handRank.handType);
        handRankID |= (handTypeID << 20);

        for (int i = 0; i < handRank.kickers.size(); ++i) {
            std::uint8_t valueID = static_cast<std::uint8_t>(handRank.kickers[i]);
            int offset = 16 - (4 * i);
            assert(offset >= 0);
            handRankID |= (valueID << offset);
        }

        return handRankID;
    };

    assert(getSetSize(hand) == 5);

    std::array<std::pair<int, Value>, 13> valueFrequencies;
    for (int i = 0; i < 13; ++i) {
        valueFrequencies[i] = { 0, static_cast<Value>(i) };
    }

    CardSet temp = hand;
    for (int i = 0; i < 5; ++i) {
        CardID lowestCard = getLowestCardInSet(temp);
        int valueID = static_cast<int>(getCardValue(lowestCard));
        ++valueFrequencies[valueID].first;
        temp &= ~cardIDToSet(lowestCard);
    }
    assert(temp == 0);
    std::sort(valueFrequencies.begin(), valueFrequencies.end(), std::greater<std::pair<int, Value>>());

    HandRank handRank;
    if (valueFrequencies[0].first == 4) {
        handRank = {
            .handType = HandType::FourOfAKind,
            .kickers = { valueFrequencies[0].second, valueFrequencies[1].second }
        };
    }
    else if (valueFrequencies[0].first == 3 && valueFrequencies[1].first == 2) {
        handRank = {
            .handType = HandType::FullHouse,
            .kickers = { valueFrequencies[0].second, valueFrequencies[1].second }
        };
    }
    else if (valueFrequencies[0].first == 3) {
        handRank = {
            .handType = HandType::ThreeOfAKind,
            .kickers = { valueFrequencies[0].second, valueFrequencies[1].second, valueFrequencies[2].second }
        };
    }
    else if (valueFrequencies[0].first == 2 && valueFrequencies[1].first == 2) {
        handRank = {
            .handType = HandType::TwoPair,
            .kickers = { valueFrequencies[0].second, valueFrequencies[1].second, valueFrequencies[2].second }
        };
    }
    else if (valueFrequencies[0].first == 2) {
        handRank = {
            .handType = HandType::Pair,
            .kickers = { valueFrequencies[0].second, valueFrequencies[1].second, valueFrequencies[2].second, valueFrequencies[3].second }
        };
    }
    else {
        // Check for straights
        std::array<Value, 5> sortedCardValues;
        CardSet temp = hand;
        for (int i = 0; i < 5; ++i) {
            CardID lowestCard = getLowestCardInSet(temp);
            Value value = getCardValue(lowestCard);
            sortedCardValues[i] = value;
            temp &= ~cardIDToSet(lowestCard);
        }
        assert(temp == 0);
        std::reverse(sortedCardValues.begin(), sortedCardValues.end());

        bool isRegularStraight = (static_cast<int>(sortedCardValues[0]) - static_cast<int>(sortedCardValues[4]) == 4);
        bool isWheelStraight = (sortedCardValues[0] == Value::Ace) && (sortedCardValues[1] == Value::Five);

        // Check for flushes
        bool isFlush = false;
        const CardSet SingleSuitMask = 0x1111111111111;
        for (int i = 0; i < 4; ++i) {
            bool isFlushThisSuit = (getSetSize(hand & (SingleSuitMask << i)) == 5);
            isFlush |= isFlushThisSuit;
        }

        // Check for straight flushes
        bool isRegularStraightFlush = isRegularStraight && isFlush;
        bool isWheelStraightFlush = isWheelStraight && isFlush;
        bool isRoyalFlush = isRegularStraightFlush && (sortedCardValues[0] == Value::Ace);

        if (isRoyalFlush) {
            handRank = {
                .handType = HandType::RoyalFlush,
                .kickers = {}
            };
        }
        else if (isRegularStraightFlush) {
            handRank = {
                .handType = HandType::StraightFlush,
                .kickers = { sortedCardValues[0] }
            };
        }
        else if (isWheelStraightFlush) {
            handRank = {
                .handType = HandType::StraightFlush,
                .kickers = { sortedCardValues[1] }
            };
        }
        else if (isFlush) {
            handRank = {
                .handType = HandType::Flush,
                .kickers = sortedCardValues
            };
        }
        else if (isRegularStraight) {
            handRank = {
                .handType = HandType::Straight,
                .kickers = { sortedCardValues[0] }
            };
        }
        else if (isWheelStraight) {
            handRank = {
                .handType = HandType::Straight,
                .kickers = { sortedCardValues[1] }
            };
        }
        else {
            handRank = {
                .handType = HandType::HighCard,
                .kickers = sortedCardValues
            };
        }
    }

    return convertHandRankToInt(handRank);
}