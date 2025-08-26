#ifndef PARSE_INPUT_HPP
#define PARSE_INPUT_HPP

#include "game/game_types.hpp"
#include "game/holdem/holdem.hpp"
#include "util/result.hpp"

#include <string>
#include <vector>

Result<CardSet> buildCommunityCardsFromStrings(const std::vector<std::string>& communityCardStrings);
Result<Holdem::Range> buildRangeFromStrings(const std::vector<std::string>& rangeStrings);

#endif // PARSE_INPUT_HPP