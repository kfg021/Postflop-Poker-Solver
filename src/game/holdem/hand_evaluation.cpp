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

struct HandStrength {
    HandType handType;
    FixedVector<Value, 5> kickers;
};

HandRank convertHandStrengthToInt(const HandStrength& handStrength) {
    // Integer representation:
    // Bits [23, 20]: Hand type (1 is high card, 10 is royal flush)
    // Bits [19, 16]: Kicker 0
    // Bits [15, 12]: Kicker 1
    // Bits [11, 8]:  Kicker 2
    // Bits [7, 4]:   Kicker 3
    // Bits [3, 0]:   Kicker 4
    // Any bits with non existient kickers are set to 0

    HandRank handRank = 0;

    // Adding 1 to the hand type to ensure that 0 is an invalid hand ranking
    std::uint8_t handTypeID = static_cast<std::uint8_t>(handStrength.handType) + 1;
    handRank |= (handTypeID << 20);

    for (int i = 0; i < handStrength.kickers.size(); ++i) {
        std::uint8_t valueID = static_cast<std::uint8_t>(handStrength.kickers[i]);
        int offset = 16 - (4 * i);
        assert(offset >= 0);
        handRank |= (valueID << offset);
    }

    return handRank;
}
} // namespace

HandRank getFiveCardHandRank(CardSet hand) {
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

    HandStrength handStrength;
    if (valueFrequencies[0].first == 4) {
        handStrength = {
            .handType = HandType::FourOfAKind,
            .kickers = { valueFrequencies[0].second, valueFrequencies[1].second }
        };
    }
    else if (valueFrequencies[0].first == 3 && valueFrequencies[1].first == 2) {
        handStrength = {
            .handType = HandType::FullHouse,
            .kickers = { valueFrequencies[0].second, valueFrequencies[1].second }
        };
    }
    else if (valueFrequencies[0].first == 3) {
        handStrength = {
            .handType = HandType::ThreeOfAKind,
            .kickers = { valueFrequencies[0].second, valueFrequencies[1].second, valueFrequencies[2].second }
        };
    }
    else if (valueFrequencies[0].first == 2 && valueFrequencies[1].first == 2) {
        handStrength = {
            .handType = HandType::TwoPair,
            .kickers = { valueFrequencies[0].second, valueFrequencies[1].second, valueFrequencies[2].second }
        };
    }
    else if (valueFrequencies[0].first == 2) {
        handStrength = {
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
        static constexpr CardSet SingleSuitMask = 0x1'1111'1111'1111;
        for (int i = 0; i < 4; ++i) {
            bool isFlushThisSuit = (getSetSize(hand & (SingleSuitMask << i)) == 5);
            isFlush |= isFlushThisSuit;
        }

        // Check for straight flushes
        bool isRegularStraightFlush = isRegularStraight && isFlush;
        bool isWheelStraightFlush = isWheelStraight && isFlush;
        bool isRoyalFlush = isRegularStraightFlush && (sortedCardValues[0] == Value::Ace);

        if (isRoyalFlush) {
            handStrength = {
                .handType = HandType::RoyalFlush,
                .kickers = {}
            };
        }
        else if (isRegularStraightFlush) {
            handStrength = {
                .handType = HandType::StraightFlush,
                .kickers = { sortedCardValues[0] }
            };
        }
        else if (isWheelStraightFlush) {
            handStrength = {
                .handType = HandType::StraightFlush,
                .kickers = { sortedCardValues[1] }
            };
        }
        else if (isFlush) {
            handStrength = {
                .handType = HandType::Flush,
                .kickers = sortedCardValues
            };
        }
        else if (isRegularStraight) {
            handStrength = {
                .handType = HandType::Straight,
                .kickers = { sortedCardValues[0] }
            };
        }
        else if (isWheelStraight) {
            handStrength = {
                .handType = HandType::Straight,
                .kickers = { sortedCardValues[1] }
            };
        }
        else {
            handStrength = {
                .handType = HandType::HighCard,
                .kickers = sortedCardValues
            };
        }
    }

    return convertHandStrengthToInt(handStrength);
}