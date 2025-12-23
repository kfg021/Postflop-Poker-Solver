#include "game/holdem/parse_input.hpp"

#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "util/user_input.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <unordered_set>
#include <vector>

Result<CardSet> buildCommunityCardsFromString(const std::string& communityCardString) {
    std::vector<std::string> communityCardStrings = parseTokens(communityCardString, ',');

    CardSet communityCards = 0;
    for (const auto& cardString : communityCardStrings) {
        Result<CardID> cardIDResult = getCardIDFromName(cardString);
        if (cardIDResult.isError()) {
            return cardIDResult.getError();
        }

        const CardID& cardID = cardIDResult.getValue();
        if (setContainsCard(communityCards, cardID)) {
            return "Error building community cards: \"" + cardString + "\" appears more than once.";
        }

        communityCards |= cardIDToSet(cardID);
    }

    int communityCardSize = getSetSize(communityCards);
    if (communityCardSize < 3 || communityCardSize > 5) {
        return "Error building community cards: Size must be 3, 4, or 5 (flop, turn, or river).";
    }

    return communityCards;
}

Result<Holdem::Range> buildRangeFromString(const std::string& rangeString) {
    return buildRangeFromString(rangeString, 0);
}

// TODO: Add support for specific hand combos
Result<Holdem::Range> buildRangeFromString(const std::string& rangeString, CardSet communityCards) {
    auto getValueFromChar = [](char c) -> Result<Value> {
        switch (c) {
            case '2': case '3': case '4': case '5':
            case '6': case '7': case '8': case '9': {
                int valueID = static_cast<int>(Value::Two) + (c - '2');
                return static_cast<Value>(valueID);
            }
            case 'T':
                return Value::Ten;
            case 'J':
                return Value::Jack;
            case 'Q':
                return Value::Queen;
            case 'K':
                return Value::King;
            case 'A':
                return Value::Ace;
            default:
                return "";
        }
    };

    std::vector<std::string> rangeStrings = parseTokens(rangeString, ',');

    if (rangeStrings.empty()) {
        return "Error building range: Range is empty.";
    }

    Holdem::Range range;
    std::unordered_set<CardSet> seenHands;

    for (const std::string& rangeElement : rangeStrings) {
        std::string errorString = "Error building range: \"" + rangeElement + "\" is not a valid range element.";

        if (rangeElement.size() < 2) {
            return errorString + " (Range element too short)";
        }

        Result<Value> value0Result = getValueFromChar(rangeElement[0]);
        if (value0Result.isError()) {
            return errorString + " (Failed to parse first character)";
        }

        Result<Value> value1Result = getValueFromChar(rangeElement[1]);
        if (value1Result.isError()) {
            return errorString + " (Failed to parse second character)";
        }

        Value value0 = value0Result.getValue();
        Value value1 = value1Result.getValue();

        if (value0 < value1) std::swap(value0, value1);

        enum class Combos : std::uint8_t {
            Default,
            Suited,
            Offsuit,
        };

        Combos combos = Combos::Default;
        if (rangeElement.size() >= 3) {
            switch (rangeElement[2]) {
                case 's':
                    combos = Combos::Suited;
                    break;
                case 'o':
                    combos = Combos::Offsuit;
                    break;
                default:
                    if (rangeElement[2] != ':') {
                        return errorString + " (Expected colon after hand)";
                    }
                    break;
            }
        }

        bool isPocketPair = (value0 == value1);
        if (isPocketPair) {
            if (combos != Combos::Default) {
                return errorString;
            }
        }

        float frequency = 1.0f;
        std::size_t colonLoc = rangeElement.find(':');
        if (colonLoc != std::string::npos) {
            std::optional<float> frequencyOption = parseFloat(rangeElement.substr(colonLoc + 1));
            if (!frequencyOption) {
                return errorString + " (Frequency is not a valid float)";
            }

            frequency = *frequencyOption;
            if (frequency <= 0.0f || frequency > 1.0f) {
                return errorString + " (Frequency must be > 0 and <= 1)";
            }
        }

        for (int suit0 = 3; suit0 >= 0; --suit0) {
            for (int suit1 = 3; suit1 >= 0; --suit1) {
                if (isPocketPair && (suit0 <= suit1)) continue;
                if ((combos == Combos::Offsuit) && (suit0 == suit1)) continue;
                if ((combos == Combos::Suited) && (suit0 != suit1)) continue;

                CardID card0 = getCardIDFromValueAndSuit(static_cast<Value>(value0), static_cast<Suit>(suit0));
                CardID card1 = getCardIDFromValueAndSuit(static_cast<Value>(value1), static_cast<Suit>(suit1));
                CardSet hand = cardIDToSet(card0) | cardIDToSet(card1);
                assert(getSetSize(hand) == 2);

                if (seenHands.find(hand) != seenHands.end()) {
                    return "Error building range: Duplicate range elements.";
                }

                bool isHandValid = (hand & communityCards) == 0;
                if (isHandValid) {
                    range.hands.push_back(hand);
                    range.weights.push_back(frequency);

                    seenHands.insert(hand);
                }
            }
        }
    }

    if (range.hands.empty()) {
        return "Error building range: No hands are possible given the starting board.";
    }

    return range;
}
