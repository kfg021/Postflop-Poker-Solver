#ifndef GAME_TYPES_HPP
#define GAME_TYPES_HPP

#include "game/nl_holdem_config.hpp"

#include <array>
#include <cstdint>

static constexpr int StandardDeckSize = nl_holdem::DeckSize;
static constexpr int MaxNumDealCards = nl_holdem::MaxNumDealCards;
static constexpr int MaxNumActions = nl_holdem::MaxNumActions;

using ActionID = std::uint8_t;
using CardID = std::uint8_t;
using CardSet = std::uint64_t;

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

enum class ShowdownResult : std::uint8_t {
    P0Win,
    P1Win,
    Tie
};

enum class Value: std::uint8_t {
    Two,
    Three,
    Four,
    Five,
    Six,
    Seven,
    Eight,
    Nine,
    Ten,
    Jack,
    Queen,
    King,
    Ace
};

enum class Suit : std::uint8_t {
    Clubs,
    Diamonds,
    Hearts,
    Spades
};

struct GameState {
    CardSet currentBoard;
    std::array<std::int32_t, 2> playerTotalWagers;
    std::int32_t deadMoney;
    Player playerToAct;
    ActionID lastAction;
    Street currentStreet;
    std::uint8_t numRaisesThisStreet;
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