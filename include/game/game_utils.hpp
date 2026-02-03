#ifndef GAME_UTILS_HPP
#define GAME_UTILS_HPP

#include "game/game_types.hpp"
#include "util/result.hpp"

#include <bit>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

constexpr Player getOpposingPlayer(Player player) {
    int playerID = static_cast<int>(player);
    assert(playerID == 0 || playerID == 1);
    return static_cast<Player>(playerID ^ 1);
}

constexpr Value getCardValue(CardID cardID) {
    assert(cardID < 52);
    return static_cast<Value>(cardID / 4);
}

constexpr Suit getCardSuit(CardID cardID) {
    assert(cardID < 52);
    return static_cast<Suit>(cardID % 4);
}

constexpr CardID getCardIDFromValueAndSuit(Value value, Suit suit) {
    int valueID = static_cast<int>(value);
    assert(valueID < 13);

    int suitID = static_cast<int>(suit);
    assert(suitID < 4);

    CardID cardID = static_cast<CardID>((valueID * 4) + suitID);
    return cardID;
}

constexpr CardID swapCardSuits(CardID cardID, Suit x, Suit y) {
    Value value = getCardValue(cardID);
    Suit suit = getCardSuit(cardID);
    if (suit == x) {
        return getCardIDFromValueAndSuit(value, y);
    }
    else if (suit == y) {
        return getCardIDFromValueAndSuit(value, x);
    }
    else {
        return cardID;
    }
}

constexpr CardSet cardIDToSet(CardID cardID) {
    assert(cardID < 52);
    return (1LL << cardID);
}

constexpr int getSetSize(CardSet cardSet) {
    return std::popcount(cardSet);
}

constexpr bool setContainsCard(CardSet cardSet, CardID cardID) {
    assert(cardID < 52);
    return (cardSet >> cardID) & 1;
}

constexpr bool doSetsOverlap(CardSet x, CardSet y) {
    return (x & y) != 0;
}

constexpr CardID getLowestCardInSet(CardSet cardSet) {
    assert(getSetSize(cardSet) > 0);

    CardID lowestCard = static_cast<CardID>(std::countr_zero(cardSet));
    assert(lowestCard < 52);
    return lowestCard;
}

constexpr CardID popLowestCardFromSet(CardSet& cardSet) {
    CardID lowestCard = getLowestCardInSet(cardSet);
    cardSet &= (cardSet - 1);
    return lowestCard;
}

constexpr CardSet filterCardsWithSuit(CardSet cardSet, Suit suit) {
    constexpr CardSet SingleSuitMask = 0x1'1111'1111'1111;
    int suitID = static_cast<int>(suit);
    return cardSet & (SingleSuitMask << suitID);
}

constexpr CardSet swapSetSuits(CardSet cardSet, Suit x, Suit y) {
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

constexpr int mapTwoSuitsToIndex(Suit x, Suit y) {
    assert(x != y);
    if (x > y) std::swap(x, y);

    int xID = static_cast<int>(x);
    int yID = static_cast<int>(y);
    int finalIndex = xID + ((yID * (yID - 1)) >> 1);
    assert(finalIndex < 6);
    return finalIndex;
}

constexpr Street getNextStreet(Street street) {
    return static_cast<Street>(static_cast<int>(street) + 1);
}

std::string getNameFromCardID(CardID cardID);
Result<CardID> getCardIDFromName(const std::string& cardName);
std::vector<std::string> getCardSetNames(CardSet cardSet);
Result<Value> getValueFromChar(char c);

#endif // GAME_UTILS_HPP