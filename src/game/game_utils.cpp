#include "game/game_utils.hpp"

#include "game/game_types.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <string>

namespace {
const std::string CardValueNames = "23456789TJQKA";
const std::string CardSuitNames = "chds";
} // namespace

Player getOpposingPlayer(Player player) {
    assert(player == Player::P0 || player == Player::P1);
    return (player == Player::P0) ? Player::P1 : Player::P0;
}

CardID getCardIDFromName(const std::string& cardName) {
    assert(cardName.size() == 2);

    std::size_t value = CardValueNames.find(cardName[0]);
    assert(value < 13);

    std::size_t suit = CardSuitNames.find(cardName[1]);
    assert(suit < 4);

    CardID cardID = static_cast<std::uint8_t>((value * 4) + suit);
    return cardID;
}

std::string getNameFromCardID(CardID cardID) {
    Value cardValue = getCardValue(cardID);
    Suit cardSuit = getCardSuit(cardID);
    std::string cardName = { CardValueNames[static_cast<int>(cardValue)], CardSuitNames[static_cast<int>(cardSuit)] };
    return cardName;
}

Value getCardValue(CardID cardID) {
    assert(cardID < 52);
    return static_cast<Value>(cardID / 4);
}

Suit getCardSuit(CardID cardID) {
    assert(cardID < 52);
    return static_cast<Suit>(cardID % 4);
}

CardSet cardIDToSet(CardID cardID) {
    assert(cardID < 52);
    return (1LL << cardID);
}

int getSetSize(CardSet cardSet) {
    return std::popcount(cardSet);
}

bool setContainsCard(CardSet cardSet, CardID cardID) {
    assert(cardID < 52);
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
    cardSet &= ~cardIDToSet(lowestCard);
    return lowestCard;
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

Street nextStreet(Street street) {
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
