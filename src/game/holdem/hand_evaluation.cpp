#include "game/holdem/hand_evaluation.hpp"

#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "util/fixed_vector.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <utility>

namespace {
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

std::uint32_t convertHandRankToInt(const HandRank& handRank) {
    // Integer representation:
    // Bits [23, 20]: Hand type (1 is high card, 10 is royal flush)
    // Bits [19, 16]: Kicker 0
    // Bits [15, 12]: Kicker 1
    // Bits [11, 8]:  Kicker 2
    // Bits [7, 4]:   Kicker 3
    // Bits [3, 0]:   Kicker 4
    // Any bits with non existient kickers are set to 0

    std::uint32_t handRankID = 0;

    // Adding 1 to the hand type to ensure that 0 is an invalid hand ranking
    std::uint8_t handTypeID = static_cast<std::uint8_t>(handRank.handType) + 1;
    handRankID |= (handTypeID << 20);

    for (int i = 0; i < handRank.kickers.size(); ++i) {
        std::uint8_t valueID = static_cast<std::uint8_t>(handRank.kickers[i]);
        int offset = 16 - (4 * i);
        assert(offset >= 0);
        handRankID |= (valueID << offset);
    }

    return handRankID;
}
} // namespace

std::uint32_t getFiveCardHandRank(CardSet hand) {
    assert(getSetSize(hand) == 5);

    std::array<std::pair<int, Value>, 13> valueFrequencies;
    for (int i = 0; i < 13; ++i) {
        valueFrequencies[i] = { 0, static_cast<Value>(i) };
    }

    CardSet temp = hand;
    for (int i = 0; i < 5; ++i) {
        int valueID = static_cast<int>(getCardValue(popLowestCardFromSet(temp)));
        ++valueFrequencies[valueID].first;
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
            sortedCardValues[i] = getCardValue(popLowestCardFromSet(temp));
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