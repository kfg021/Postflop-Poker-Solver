#ifndef PARSE_INPUT_HPP
#define PARSE_INPUT_HPP

#include "game/game_types.hpp"
#include "util/fixed_vector.hpp"
#include "util/result.hpp"

#include <string>
#include <vector>

// struct GameSettings {
//         // Valid input is a string of 3-5 unique card names, separated by spaces
//         // First char: rank (23456789TJQKA)
//         // Second char: suit (chds)
//         // Example: Ah 7c 2s 3d
//         std::vector<std::string> startingCommunityCards;

//         std::vector<std::string> ipRange;
//         std::string oopRange;

//         // Bet sizes are percentages of the pot
//         // TODO: Allow choosing different bet/raise sizes for each street
//         FixedVector<int, holdem::MaxNumBetSizes> betSizes;
//         FixedVector<int, holdem::MaxNumRaiseSizes> raiseSizes;

//         int startingPlayerWagers;
//         int effectiveStack;
//         int deadMoney;
//         int raiseLimit;

//         // TODO: Add all in threshold, merging
//     };

Result<CardSet> buildCommunityCardsFromVector(const std::vector<std::string>& communityCardsString);

#endif // PARSE_INPUT_HPP