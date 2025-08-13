#ifndef GAME_UTILS_HPP
#define GAME_UTILS_HPP

#include "game/game_types.hpp"
#include "util/result.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Player functions
Player getOpposingPlayer(Player player);

// CardID functions
Value getCardValue(CardID cardID);
Suit getCardSuit(CardID cardID);
std::string getNameFromCardID(CardID cardID);
Result<CardID> getCardIDFromName(const std::string& cardName);

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