#ifndef HOLDEM_CONFIG_HPP
#define HOLDEM_CONFIG_HPP

#include <algorithm>
#include <cstdint>

namespace holdem {
static constexpr int DeckSize = 52;
static constexpr int MaxNumDealCards = DeckSize - 3; // Three flop cards are already dealt

static constexpr int MaxNumBetSizes = 3;
static constexpr int MaxNumRaiseSizes = 3;

// Facing a Check: Valid actions are [Check, Bet, All-in] (MaxNumBetSizes + 2)
// Facing a Bet: valid actions are [Fold, Call, Raise, All-in] (MaxNumRaiseSizes + 3)
static constexpr int MaxNumActions = std::max(MaxNumBetSizes + 2, MaxNumRaiseSizes + 3);
} // namespace holdem

#endif // HOLDEM_CONFIG_HPP