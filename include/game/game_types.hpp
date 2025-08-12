#ifndef GAME_TYPES_HPP
#define GAME_TYPES_HPP

#include "game/holdem/config.hpp"

#include <array>
#include <cstdint>

constexpr int StandardDeckSize = holdem::DeckSize;
constexpr int MaxNumDealCards = holdem::MaxNumDealCards;
constexpr int MaxNumActions = holdem::MaxNumActions;

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

enum class Value : std::uint8_t {
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
    std::array<int, 2> playerTotalWagers;
    int deadMoney;
    Player playerToAct;
    ActionID lastAction;
    Street currentStreet;
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