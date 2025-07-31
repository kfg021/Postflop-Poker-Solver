#ifndef NL_HOLDEM_CONFIG_HPP
#define NL_HOLDEM_CONFIG_HPP

#include <algorithm>
#include <cstdint>

namespace nl_holdem {
static constexpr std::uint8_t DeckSize = 52;
static constexpr std::uint8_t MaxNumDealCards = DeckSize - 3; // Three flop cards are already dealt

static constexpr std::uint8_t MaxNumBetSizes = 3;
static constexpr std::uint8_t MaxNumRaiseSizes = 3;

// Facing a Check: Valid actions are [Check, Bet, All-in] (MaxNumBetSizes + 2)
// Facing a Bet: valid actions are [Fold, Call, Raise, All-in] (MaxNumRaiseSizes + 3)
static constexpr std::uint8_t MaxNumActions = std::max(MaxNumBetSizes + 2, MaxNumRaiseSizes + 3);
} // namespace nl_holdem

#endif // NL_HOLDEM_CONFIG_HPP