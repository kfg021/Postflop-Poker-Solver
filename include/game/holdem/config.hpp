#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <algorithm>

namespace holdem {
constexpr int DeckSize = 52;
constexpr int MaxNumDealCards = DeckSize - 3; // Three flop cards are already dealt

constexpr int MaxNumBetSizes = 3;
constexpr int MaxNumRaiseSizes = 3;

// Facing a Check: Valid actions are [Check, Bet, All-in] (MaxNumBetSizes + 2)
// Facing a Bet: valid actions are [Fold, Call, Raise, All-in] (MaxNumRaiseSizes + 3)
constexpr int MaxNumActions = std::max(MaxNumBetSizes + 2, MaxNumRaiseSizes + 3);
} // namespace holdem

#endif // CONFIG_HPP