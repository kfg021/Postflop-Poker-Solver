#ifndef HAND_EVALUATION_HPP
#define HAND_EVALUATION_HPP

#include "game/game_types.hpp"

#include <cstdint>

namespace hand_evaluation {
void buildLookupTablesIfNeeded();
std::uint32_t getFiveCardHandRank(CardSet hand);
std::uint32_t getSevenCardHandRank(CardSet hand);
} // namespace hand_evaluation

#endif // HAND_EVALUATION_HPP