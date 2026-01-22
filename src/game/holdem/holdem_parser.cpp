#include "game/holdem/holdem_parser.hpp"

#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "util/result.hpp"
#include "util/string_utils.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
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
    std::vector<std::string> rangeStrings = parseTokens(rangeString, ',');

    if (rangeStrings.empty()) {
        return "Error building range: Range is empty.";
    }

    Holdem::Range range;
    std::unordered_set<CardSet> seenHands;

    for (const std::string& rangeElement : rangeStrings) {
        std::string errorString = "Error building range: \"" + rangeElement + "\" is not a valid range element. ";

        std::size_t colonLoc = rangeElement.find(':');

        std::string handClassString = (colonLoc != std::string::npos) ? rangeElement.substr(0, colonLoc) : rangeElement;
        Result<std::vector<CardSet>> handClass = getHandClassFromString(handClassString);
        if (handClass.isError()) {
            return errorString + handClass.getError();
        }

        float frequency = 1.0f;
        if (colonLoc != std::string::npos) {
            std::optional<float> frequencyOption = parseFloat(rangeElement.substr(colonLoc + 1));
            if (!frequencyOption) {
                return errorString + "(Frequency is not a valid float)";
            }

            frequency = *frequencyOption;
            if (frequency <= 0.0f || frequency > 1.0f) {
                return errorString + "(Frequency must be > 0 and <= 1)";
            }
        }

        for (CardSet hand : handClass.getValue()) {
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

    if (range.hands.empty()) {
        return "Error building range: No hands are possible given the starting board.";
    }

    return range;
}

Result<std::vector<CardSet>> getHandClassFromString(const std::string& handClassString) {
    static const std::string ErrorPrefix = "Error parsing hand class: ";

    if ((handClassString.size() != 2) && (handClassString.size() != 3)) {
        return ErrorPrefix + "String is incorrect length.";
    }

    Result<Value> value0Result = getValueFromChar(handClassString[0]);
    if (value0Result.isError()) {
        return ErrorPrefix + "First character is not a valid card value.";
    }

    Result<Value> value1Result = getValueFromChar(handClassString[1]);
    if (value1Result.isError()) {
        return ErrorPrefix + "Second character is not a valid card value.";
    }

    Value value0 = value0Result.getValue();
    Value value1 = value1Result.getValue();

    if (value0 < value1) std::swap(value0, value1);

    enum class Combos : std::uint8_t {
        Default,
        Suited,
        Offsuit
    };

    Combos combos = Combos::Default;
    if (handClassString.size() == 3) {
        switch (handClassString[2]) {
            case 's':
                combos = Combos::Suited;
                break;
            case 'o':
                combos = Combos::Offsuit;
                break;
            default:
                return ErrorPrefix + "Third character must be \"s\" or \"o\".";
        }
    }

    bool isPocketPair = (value0 == value1);
    if (isPocketPair && combos != Combos::Default) {
        return ErrorPrefix + "Pocket pairs cannot be suited or offsuit";
    }

    std::vector<CardSet> hands;
    for (int suit0 = 3; suit0 >= 0; --suit0) {
        for (int suit1 = 3; suit1 >= 0; --suit1) {
            if (isPocketPair && (suit0 <= suit1)) continue;
            if ((combos == Combos::Offsuit) && (suit0 == suit1)) continue;
            if ((combos == Combos::Suited) && (suit0 != suit1)) continue;

            CardID card0 = getCardIDFromValueAndSuit(static_cast<Value>(value0), static_cast<Suit>(suit0));
            CardID card1 = getCardIDFromValueAndSuit(static_cast<Value>(value1), static_cast<Suit>(suit1));

            CardSet hand = cardIDToSet(card0) | cardIDToSet(card1);
            assert(getSetSize(hand) == 2);
            hands.push_back(hand);
        }
    }

    return hands;
}