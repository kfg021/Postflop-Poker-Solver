#include "game/game_utils.hpp"

#include "game/game_types.hpp"
#include "util/result.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>

namespace {
constexpr char CardValueNames[] = "23456789TJQKA";
constexpr char CardSuitNames[] = "cdhs";
} // namespace

std::string getNameFromCardID(CardID cardID) {
    assert(cardID < 52);
    Value cardValue = getCardValue(cardID);
    Suit cardSuit = getCardSuit(cardID);
    std::string cardName = { CardValueNames[static_cast<int>(cardValue)], CardSuitNames[static_cast<int>(cardSuit)] };
    return cardName;
}

Result<CardID> getCardIDFromName(const std::string& cardName) {
    std::string errorString = "Error getting card ID: \"" + cardName + "\" is not a valid card name.";

    if (cardName.size() != 2) {
        return errorString + " (Incorrect card name size)";
    }

    std::size_t valueID = std::string_view{ CardValueNames }.find(cardName[0]);
    if (valueID >= 13) {
        return errorString + " (Invalid value)";
    }

    std::size_t suitID = std::string_view{ CardSuitNames }.find(cardName[1]);
    if (suitID >= 4) {
        return errorString + " (Invalid suit)";
    }

    return (valueID * 4) + suitID;
}

std::vector<std::string> getCardSetNames(CardSet cardSet) {
    int setSize = getSetSize(cardSet);
    std::vector<std::string> cardNames(setSize);
    for (int i = 0; i < setSize; ++i) {
        cardNames[i] = getNameFromCardID(popLowestCardFromSet(cardSet));
    }
    assert(cardSet == 0);

    // Descending order
    std::reverse(cardNames.begin(), cardNames.end());
    return cardNames;
}

Result<Value> getValueFromChar(char c) {
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
            return "Error: " + std::string{ c } + " is not a valid card value.";
    }
}