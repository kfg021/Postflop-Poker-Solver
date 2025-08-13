#ifndef PARSE_INPUT_HPP
#define PARSE_INPUT_HPP

#include "game/game_types.hpp"
#include "game/holdem/holdem.hpp"
#include "util/result.hpp"

#include <string>
#include <vector>

Result<CardSet> buildCommunityCardsFromVector(const std::vector<std::string>& communityCardsVector);
Result<std::vector<Holdem::RangeElement>> buildRangeFromVector(const std::vector<std::string>& rangeVector);

#endif // PARSE_INPUT_HPP