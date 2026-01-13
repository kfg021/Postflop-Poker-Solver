#include "game/game_utils.hpp"

#include "game/game_types.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

namespace {
constexpr char CardValueNames[] = "23456789TJQKA";
constexpr char CardSuitNames[] = "cdhs";
} // namespace

Player getOpposingPlayer(Player player) {
    int playerID = static_cast<int>(player);
    assert(playerID == 0 || playerID == 1);
    return static_cast<Player>(playerID ^ 1);
}

Value getCardValue(CardID cardID) {
    assert(cardID < 52);
    return static_cast<Value>(cardID / 4);
}

Suit getCardSuit(CardID cardID) {
    assert(cardID < 52);
    return static_cast<Suit>(cardID % 4);
}

CardID getCardIDFromValueAndSuit(Value value, Suit suit) {
    int valueID = static_cast<int>(value);
    assert(valueID < 13);

    int suitID = static_cast<int>(suit);
    assert(suitID < 4);

    CardID cardID = static_cast<CardID>((valueID * 4) + suitID);
    return cardID;
}

std::string getNameFromCardID(CardID cardID) {
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

CardSet cardIDToSet(CardID cardID) {
    assert(cardID < 52);
    return (1LL << cardID);
}

int getSetSize(CardSet cardSet) {
    return std::popcount(cardSet);
}

bool setContainsCard(CardSet cardSet, CardID cardID) {
    return (cardSet >> cardID) & 1;
}

CardID getLowestCardInSet(CardSet cardSet) {
    assert(getSetSize(cardSet) > 0);

    CardID lowestCard = static_cast<CardID>(std::countr_zero(cardSet));
    assert(lowestCard < 52);
    return lowestCard;
}

CardID popLowestCardFromSet(CardSet& cardSet) {
    CardID lowestCard = getLowestCardInSet(cardSet);
    cardSet &= (cardSet - 1);
    return lowestCard;
}

CardSet filterCardsWithSuit(CardSet cardSet, Suit suit) {
    static constexpr CardSet SingleSuitMask = 0x1'1111'1111'1111;
    int suitID = static_cast<int>(suit);
    return cardSet & (SingleSuitMask << suitID);
}

CardSet swapSuits(CardSet cardSet, Suit x, Suit y) {
    assert(x != y);
    if (x > y) std::swap(x, y);

    CardSet suit0Masked = filterCardsWithSuit(cardSet, x);
    CardSet suit1Masked = filterCardsWithSuit(cardSet, y);

    int diff = static_cast<int>(y) - static_cast<int>(x);
    assert(diff > 0);

    cardSet &= ~(suit0Masked | suit1Masked);
    cardSet |= (suit0Masked << diff) | (suit1Masked >> diff);

    return cardSet;
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

Street getNextStreet(Street street) {
    switch (street) {
        case Street::Flop:
            return Street::Turn;
        case Street::Turn:
            return Street::River;
        default:
            assert(false);
            return Street::River;
    }
}
