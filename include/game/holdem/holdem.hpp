#ifndef HOLDEM_HPP
#define HOLDEM_HPP

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/holdem/config.hpp"
#include "util/fixed_vector.hpp"
#include "util/result.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Holdem final : public IGameRules {
public:
    struct RangeElement {
        CardSet hand;
        int percentage;

        auto operator<=>(const RangeElement&) const = default;
    };

    struct Settings {
        PlayerArray<std::vector<RangeElement>> ranges;
        CardSet startingCommunityCards;
        FixedVector<int, holdem::MaxNumBetSizes> betSizes;
        FixedVector<int, holdem::MaxNumRaiseSizes> raiseSizes;
        int startingPlayerWagers;
        int effectiveStack;
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
    ShowdownResult getShowdownResult(CardSet player0Hand, CardSet player1Hand, CardSet board) const override;
    std::string getActionName(ActionID actionID) const override;

private:
    Settings m_settings;
};

#endif // HOLDEM_HPP