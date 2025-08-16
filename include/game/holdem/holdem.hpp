#ifndef HOLDEM_HPP
#define HOLDEM_HPP

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/holdem/config.hpp"
#include "util/fixed_vector.hpp"
#include "util/result.hpp"

#include <cstdint>
#include <string>
#include <vector>

class Holdem final : public IGameRules {
public:
    struct RangeElement {
        RangeElement(CardSet hand_, int frequency_);

        CardSet hand;
        int frequency;

        auto operator<=>(const RangeElement&) const = default;
    };

    struct Settings {
        PlayerArray<std::vector<RangeElement>> ranges;
        CardSet startingCommunityCards;
        FixedVector<int, holdem::MaxNumBetSizes> betSizes;
        FixedVector<int, holdem::MaxNumRaiseSizes> raiseSizes;
        int startingPlayerWagers;
        int effectiveStackRemaining;
        int deadMoney;

        // TODO:
        // Add all-in threshold
        // Force all-in threshold
        // Merging threshold
        // Raise limit?
    };

    Holdem(const Settings& settings);

    GameState getInitialGameState() const override;
    NodeType getNodeType(const GameState& state) const override;
    ActionType getActionType(ActionID actionID) const override;
    FixedVector<ActionID, MaxNumActions> getValidActions(const GameState& state) const override;
    GameState getNewStateAfterDecision(const GameState& state, ActionID actionID) const override;
    std::uint16_t getRangeSize(Player player) const override;
    std::vector<InitialSetup> getInitialSetups() const override;
    CardSet getDeck() const override;
    CardSet mapIndexToHand(Player player, std::uint16_t index) const override;
    ShowdownResult getShowdownResult(PlayerArray<std::uint16_t> handIndices, CardSet board) const override;
    std::string getActionName(ActionID actionID) const override;

private:
    void buildHandRankTables();
    int getTotalEffectiveStack() const;

    Settings m_settings;
    PlayerArray<std::vector<std::int32_t>> m_handRanks;
    Street m_startingStreet;
};

#endif // HOLDEM_HPP