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
CardID getCardIDFromValueAndSuit(Value value, Suit suit);
CardID swapCardSuits(CardID cardID, Suit x, Suit y);
std::string getNameFromCardID(CardID cardID);
Result<CardID> getCardIDFromName(const std::string& cardName);

// CardSet functions
CardSet cardIDToSet(CardID cardID);
int getSetSize(CardSet cardSet);
bool setContainsCard(CardSet cardSet, CardID cardID);
bool doSetsOverlap(CardSet x, CardSet y);
CardID getLowestCardInSet(CardSet cardSet);
CardID popLowestCardFromSet(CardSet& cardSet);
CardSet filterCardsWithSuit(CardSet cardSet, Suit suit);
CardSet swapSetSuits(CardSet cardSet, Suit x, Suit y);
std::vector<std::string> getCardSetNames(CardSet cardSet);

// Value functions
Result<Value> getValueFromChar(char c);

// Street functions
Street getNextStreet(Street street);

#endif // GAME_UTILS_HPP