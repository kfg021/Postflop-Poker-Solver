#include "game/holdem/hand_evaluation.hpp"

#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "util/fixed_vector.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>

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
    // Kickers are represented as their value for non face cards (2-T)
    // For face cards: J=11, Q=12, K=13, A=14
    // Any bits with non existient kickers are set to 0

    HandRank handRank = 0;

    // Adding 1 to the hand type to ensure that 0 is an invalid hand ranking
    std::uint8_t handTypeID = static_cast<std::uint8_t>(handStrength.handType) + 1;
    handRank |= (handTypeID << 20);

    for (int i = 0; i < handStrength.kickers.size(); ++i) {
        // Adding 2 so that valueID is [2, 14]
        std::uint8_t valueID = static_cast<std::uint8_t>(handStrength.kickers[i]) + 2;
        assert(valueID >= 2 && valueID <= 14);

        int offset = 16 - (4 * i);
        assert(offset >= 0);
        handRank |= (valueID << offset);
    }

    return handRank;
}
} // namespace

HandRank getFiveCardHandRank(CardSet hand) {
    assert(getSetSize(hand) == 5);

    struct ValueFrequency {
        int count;
        Value value;

        auto operator<=>(const ValueFrequency&) const = default;
    };

    std::array<ValueFrequency, 13> valueFrequencies;
    for (int i = 0; i < 13; ++i) {
        valueFrequencies[i] = { .count = 0, .value = static_cast<Value>(i) };
    }

    CardSet temp = hand;
    for (int i = 0; i < 5; ++i) {
        int valueID = static_cast<int>(getCardValue(popLowestCardFromSet(temp)));
        ++valueFrequencies[valueID].count;
    }
    assert(temp == 0);

    FixedVector<Value, 5> singles;
    FixedVector<Value, 2> pairs;
    FixedVector<Value, 1> trips;
    FixedVector<Value, 1> quads;
    for (int i = 12; i >= 0; --i) {
        auto [count, value] = valueFrequencies[i];
        switch (count) {
            case 1:
                singles.pushBack(value);
                break;
            case 2:
                pairs.pushBack(value);
                break;
            case 3:
                trips.pushBack(value);
                break;
            case 4:
                quads.pushBack(value);
                break;
            default:
                assert(false);
                break;
        }
    }

    HandStrength handStrength;
    if (quads.size() == 1) {
        assert(singles.size() == 1);
        handStrength = {
            .handType = HandType::FourOfAKind,
            .kickers = { quads[0], singles[0] }
        };
    }
    else if (trips.size() == 1 && pairs.size() == 1) {
        handStrength = {
            .handType = HandType::FullHouse,
            .kickers = { trips[0], pairs[0] }
        };
    }
    else if (trips.size() == 1) {
        assert(singles.size() == 2);
        handStrength = {
            .handType = HandType::ThreeOfAKind,
            .kickers = { trips[0], singles[0], singles[1] }
        };
    }
    else if (pairs.size() == 2) {
        assert(singles.size() == 1);
        handStrength = {
            .handType = HandType::TwoPair,
            .kickers = { pairs[0], pairs[1], singles[0] }
        };
    }
    else if (pairs.size() == 1) {
        assert(singles.size() == 3);
        handStrength = {
            .handType = HandType::Pair,
            .kickers = { pairs[0], singles[0], singles[1], singles[2] }
        };
    }
    else {
        std::uint16_t cardValues = 0;
        CardSet temp = hand;
        for (int i = 0; i < 5; ++i) {
            int cardValueID = static_cast<int>(getCardValue(popLowestCardFromSet(temp)));
            cardValues |= (1 << cardValueID);
        }
        assert(temp == 0);

        static constexpr std::uint16_t RegularStraightMask = 0x001F;
        bool isRegularStraight = false;
        for (int i = 0; i < 9; ++i) {
            isRegularStraight |= (cardValues == (RegularStraightMask << i));
        }

        static constexpr std::uint16_t WheelStraightMask = 0x100F;
        bool isWheelStraight = (cardValues == WheelStraightMask);

        bool isFlush = false;
        for (Suit suit : { Suit::Clubs, Suit::Diamonds, Suit::Hearts, Suit::Spades }) {
            isFlush |= getSetSize(filterCardsWithSuit(hand, suit)) == 5;
        }

        bool isRegularStraightFlush = isRegularStraight && isFlush;
        bool isWheelStraightFlush = isWheelStraight && isFlush;

        bool cardValuesContainAce = (cardValues >> static_cast<int>(Value::Ace)) & 1;
        bool isRoyalFlush = isRegularStraightFlush && cardValuesContainAce;

        if (isRoyalFlush) {
            handStrength = {
                .handType = HandType::RoyalFlush,
                .kickers = {}
            };
        }
        else if (isRegularStraightFlush) {
            Value highestValue = static_cast<Value>(15 - std::countl_zero(cardValues));
            handStrength = {
                .handType = HandType::StraightFlush,
                .kickers = { highestValue }
            };
        }
        else if (isWheelStraightFlush) {
            handStrength = {
                .handType = HandType::StraightFlush,
                .kickers = { Value::Five }
            };
        }
        else if (isFlush) {
            handStrength = {
                .handType = HandType::Flush,
                .kickers = singles
            };
        }
        else if (isRegularStraight) {
            Value highestValue = static_cast<Value>(15 - std::countl_zero(cardValues));
            handStrength = {
                .handType = HandType::Straight,
                .kickers = { highestValue }
            };
        }
        else if (isWheelStraight) {
            handStrength = {
                .handType = HandType::Straight,
                .kickers = { Value::Five }
            };
        }
        else {
            handStrength = {
                .handType = HandType::HighCard,
                .kickers = singles
            };
        }
    }

    return convertHandStrengthToInt(handStrength);
}