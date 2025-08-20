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

template <typename T>
class PlayerArray {
public:
    constexpr PlayerArray() = default;
    constexpr PlayerArray(const T& player0Value, const T& player1Value) : m_array{ player0Value, player1Value } {};

    constexpr const T& operator[](Player player) const {
        return m_array[getPlayerID(player)];
    }

    constexpr T& operator[](Player player) {
        return m_array[getPlayerID(player)];
    }

    constexpr bool operator==(const PlayerArray&) const = default;

private:
    constexpr int getPlayerID(Player player) const {
        return (player == Player::P0) ? 0 : 1;
    }

    std::array<T, 2> m_array;
};

struct GameState {
    CardSet currentBoard;
    PlayerArray<int> totalWagers;
    int deadMoney;
    Player playerToAct;
    ActionID lastAction;
    Street currentStreet;
};

struct InitialSetup {
    InitialSetup(
        PlayerArray<std::uint16_t> handIndices_,
        PlayerArray<float> weights_,
        float matchupProbability_
    ) :
        handIndices{ handIndices_ },
        weights{ weights_ },
        matchupProbability{ matchupProbability_ } {
    }

    PlayerArray<std::uint16_t> handIndices;
    PlayerArray<float> weights;
    float matchupProbability;
};

#endif // GAME_TYPES_HPP