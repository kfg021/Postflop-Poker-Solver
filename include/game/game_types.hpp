#ifndef GAME_TYPES_HPP
#define GAME_TYPES_HPP

#include "game/nl_holdem_config.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

using ActionID = std::uint8_t;
using CardID = std::uint8_t;
using CardSet = std::uint64_t;

static constexpr std::uint8_t StandardDeckSize = nl_holdem::DeckSize;
static constexpr std::uint8_t MaxNumDealCards = nl_holdem::MaxNumDealCards;
static constexpr std::uint8_t MaxNumActions = nl_holdem::MaxNumActions;

enum class Player : std::uint8_t {
    P0,
    P1
};

enum class Street : std::uint8_t {
    Flop,
    Turn,
    River
};

enum class NodeType : std::uint8_t {
    Chance,
    Decision,
    Fold,
    Showdown
};

enum class ActionType : std::uint8_t {
    Chance,
    Decision
};

// TODO: chop pots
struct GameState {
    CardSet currentBoard;
    std::array<std::int32_t, 2> playerTotalWagers;
    std::int32_t deadMoney;
    Player playerToAct;
    ActionID lastAction;
    Street currentStreet;
    bool isStartOfStreet;
};

struct InitialSetup {
    InitialSetup(
        const std::array<CardSet, 2>& playerHands_,
        const std::array<float, 2>& playerWeights_,
        float matchupProbability_
    ) :
        playerHands{ playerHands_ },
        playerWeights{ playerWeights_ },
        matchupProbability{ matchupProbability_ } {
    }

    std::array<CardSet, 2> playerHands;
    std::array<float, 2> playerWeights;
    float matchupProbability;
};

#endif // GAME_TYPES_HPP