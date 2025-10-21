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

    struct Settings {
        PlayerArray<Range> ranges;
        CardSet startingCommunityCards;
        FixedVector<int, holdem::MaxNumBetSizes> betSizes;
        FixedVector<int, holdem::MaxNumRaiseSizes> raiseSizes;
        int startingPlayerWagers;
        int effectiveStackRemaining;
        int deadMoney;

        // TODO:
        // Different sizes for each street
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
    FixedVector<GameState, MaxNumDealCards> getNewStatesAfterChance(const GameState& state) const override;
    const std::vector<CardSet>& getRangeHands(Player player) const override;
    const std::vector<float>& getInitialRangeWeights(Player player) const override;
    std::span<const HandData> getSortedHandRanks(Player player, CardSet board) const override;
    std::string getActionName(ActionID actionID, int betRaiseSize) const override;

private:
    void buildHandRankTables();
    int getTotalEffectiveStack() const;
    bool areBothPlayersAllIn(const GameState& state) const;
    Street getStartingStreet() const;

    Settings m_settings;
    PlayerArray<std::vector<HandData>> m_handRanks;
};

#endif // HOLDEM_HPP