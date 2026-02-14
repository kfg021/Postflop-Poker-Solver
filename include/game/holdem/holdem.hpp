#ifndef HOLDEM_HPP
#define HOLDEM_HPP

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/holdem/config.hpp"
#include "util/fixed_vector.hpp"
#include "util/result.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

class Holdem final : public IGameRules {
public:
    struct Range {
        std::vector<CardSet> hands;
        std::vector<float> weights;

        bool operator==(const Range&) const = default;
    };

    using BetSizes = PlayerArray<StreetArray<FixedVector<int, holdem::MaxNumBetSizes>>>;
    using RaiseSizes = PlayerArray<StreetArray<FixedVector<int, holdem::MaxNumRaiseSizes>>>;

    struct Settings {
        PlayerArray<Range> ranges;
        CardSet startingCommunityCards;
        BetSizes betSizes;
        RaiseSizes raiseSizes;
        int startingPlayerWagers;
        int effectiveStackRemaining;
        int deadMoney;
        bool useChanceCardIsomorphism;
        int numThreads;

        // TODO:
        // Add all-in threshold
        // Force all-in threshold
        // Merging threshold
        // Raise limit?
        // Rake?
    };

    Holdem(const Settings& settings);

    GameState getInitialGameState() const override;
    CardSet getDeck() const override;
    int getDeadMoney() const override;
    NodeType getNodeType(const GameState& state) const override;
    FixedVector<ActionID, MaxNumActions> getValidActions(const GameState& state) const override;
    GameState getNewStateAfterDecision(const GameState& state, ActionID actionID) const override;
    FixedVector<SuitEquivalenceClass, 4> getChanceNodeIsomorphisms(CardSet board) const override;
    std::span<const CardSet> getRangeHands(Player player) const override;
    int getHandIndexAfterSuitSwap(Player player, int handIndex, Suit x, Suit y) const override;
    std::span<const float> getInitialRangeWeights(Player player) const override;
    std::span<const HandInfo> getValidHands(Player player, CardSet board) const override;
    std::span<const RankedHand> getValidSortedHandRanks(Player player, CardSet board) const override;
    std::string getActionName(ActionID actionID, int betRaiseSize) const override;

private:
    void buildHandTables();
    HandInfo getHandInfo(Player player, int handIndex) const;
    int getTotalEffectiveStack() const;
    bool areBothPlayersAllIn(const GameState& state) const;
    Street getStartingStreet() const;

    Settings m_settings;
    PlayerArray<std::vector<HandInfo>> m_validHands;
    PlayerArray<std::vector<RankedHand>> m_handRanks;
    PlayerArray<std::array<std::int16_t, holdem::NumPossibleTwoCardHands>> m_handToRangeIndex;
    FixedVector<SuitEquivalenceClass, 4> m_startingIsomorphisms;
    std::array<FixedVector<SuitEquivalenceClass, 4>, 4> m_isomorphismsAfterSuitDealt;
};

#endif // HOLDEM_HPP