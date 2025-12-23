#ifndef PARSE_INPUT_HPP
#define PARSE_INPUT_HPP

#include "game/game_types.hpp"
#include "game/holdem/holdem.hpp"
#include "util/result.hpp"

#include <string>
#include <vector>

Result<CardSet> buildCommunityCardsFromString(const std::string& communityCardString);
Result<Holdem::Range> buildRangeFromString(const std::string& rangeString);
Result<Holdem::Range> buildRangeFromString(const std::string& rangeString, CardSet communityCards);

#endif // PARSE_INPUT_HPP