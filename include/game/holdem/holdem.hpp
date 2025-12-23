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

        // TODO:
        // Add all-in threshold
        // Force all-in threshold
        // Merging threshold
        // Raise limit?
        // Rake?
    };

    Holdem(const Settings& settings);

    GameState getInitialGameState() const override;
    int getDeadMoney() const override;
    NodeType getNodeType(const GameState& state) const override;
    FixedVector<ActionID, MaxNumActions> getValidActions(const GameState& state) const override;
    GameState getNewStateAfterDecision(const GameState& state, ActionID actionID) const override;
    ChanceNodeInfo getChanceNodeInfo(CardSet board) const override;
    std::span<const CardSet> getRangeHands(Player player) const override;
    std::span<const float> getInitialRangeWeights(Player player) const override;
    std::span<const HandData> getValidSortedHandRanks(Player player, CardSet board) const override;
    int getHandIndexAfterSuitSwap(Player player, int handIndex, Suit x, Suit y) const override;
    std::string getActionName(ActionID actionID, int betRaiseSize) const override;

private:
    void buildHandTables();
    int getTotalEffectiveStack() const;
    bool areBothPlayersAllIn(const GameState& state) const;
    Street getStartingStreet() const;

    Settings m_settings;
    PlayerArray<std::vector<HandData>> m_handRanks;
    PlayerArray<std::array<int, holdem::NumPossibleTwoCardHands>> m_handIndices;
    FixedVector<SuitEquivalenceClass, 4> m_startingIsomorphisms;
    std::array<FixedVector<SuitEquivalenceClass, 4>, 4> m_isomorphismsAfterSuitDealt;
};

#endif // HOLDEM_HPP