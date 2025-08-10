#ifndef GAME_UTILS_HPP
#define GAME_UTILS_HPP

#include "game_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Player functions
Player getOpposingPlayer(Player player);
std::uint8_t getPlayerID(Player player);
std::uint8_t getOpposingPlayerID(Player player);

// CardID functions
CardID getCardIDFromName(const std::string& cardName);
std::string getNameFromCardID(CardID cardID);
Value getCardValue(CardID cardID);
Suit getCardSuit(CardID cardID);

// CardSet functions
CardSet cardIDToSet(CardID cardID);
int getSetSize(CardSet cardSet);
bool setContainsCard(CardSet cardSet, CardID cardID);
CardID getLowestCardInSet(CardSet cardSet);
CardID popLowestCardFromSet(CardSet& cardSet);
std::vector<std::string> getCardSetNames(CardSet cardSet);

// Street functions
Street nextStreet(Street street);

#endif // GAME_UTILS_HPP