#include "game/game_utils.hpp"

#include "game/game_types.hpp"

#include <bit>
#include <cstdint>
#include <string>

Player getOpposingPlayer(Player player) {
    assert(player == Player::P0 || player == Player::P1);
    return (player == Player::P0) ? Player::P1 : Player::P0;
}

std::uint8_t getPlayerID(Player player) {
    assert(player == Player::P0 || player == Player::P1);
    return (player == Player::P0) ? 0 : 1;
}

std::uint8_t getOpposingPlayerID(Player player) {
    return getPlayerID(getOpposingPlayer(player));
}

CardID getCardIDFromName(const std::string& cardName) {
    assert(cardName.size() == 2);

    static const std::string CardValues = "23456789TJQKA";
    std::size_t value = CardValues.find(cardName[0]);
    assert(value < 13);

    static const std::string CardSuits = "chds";
    std::size_t suit = CardSuits.find(cardName[1]);
    assert(suit < 4);

    CardID cardID = static_cast<std::uint8_t>((value * 4) + suit);
    return cardID;
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

std::uint8_t getSetSize(CardSet cardSet) {
    return std::popcount(cardSet);
}

bool setContainsCard(CardSet cardSet, CardID cardID) {
    assert(cardID < 52);
    return (cardSet >> cardID) & 1;
}

CardID getLowestCardInSet(CardSet cardSet) {
    CardID lowestCard = static_cast<CardID>(std::countr_zero(cardSet));
    assert(lowestCard < 52);
    return lowestCard;
}

CardSet removeCardFromSet(CardSet cardSet, CardID cardID) {
    assert(setContainsCard(cardSet, cardID));
    return cardSet & ~cardIDToSet(cardID);
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
