#ifndef GAME_UTILS_HPP
#define GAME_UTILS_HPP

#include "game_types.hpp"

#include <cstdint>
#include <string>

Player getOpposingPlayer(Player player);
std::uint8_t getPlayerID(Player player);
std::uint8_t getOpposingPlayerID(Player player);

CardID getCardIDFromName(const std::string& cardName);
Value getCardValue(CardID cardID);
Suit getCardSuit(CardID cardID);

CardSet cardIDToSet(CardID cardID);
int getSetSize(CardSet cardSet);
bool setContainsCard(CardSet cardSet, CardID cardID);
CardID getLowestCardInSet(CardSet cardSet);
CardSet removeCardFromSet(CardSet cardSet, CardID cardID);

Street nextStreet(Street street);
// std::string getNameFromCardID(CardID cardID);
// std::vector<std::string> getNamesFromCardSet(CardSet cardSet);

#endif // GAME_UTILS_HPP