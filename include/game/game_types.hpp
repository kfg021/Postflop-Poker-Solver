#ifndef GAME_TYPES_HPP
#define GAME_TYPES_HPP

#include "game/holdem/config.hpp"
#include "util/fixed_vector.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

constexpr int StandardDeckSize = holdem::DeckSize;
constexpr int MaxNumDealCards = holdem::MaxNumDealCards;
constexpr int MaxNumActions = holdem::MaxNumActions;

using ActionID = std::uint8_t;
using CardID = std::uint8_t;
using CardSet = std::uint64_t;
using HandRank = std::uint32_t;

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
        int playerID = static_cast<int>(player);
        assert(playerID == 0 || playerID == 1);
        return playerID;
    }

    std::array<T, 2> m_array;
};

struct GameState {
    CardSet currentBoard;
    PlayerArray<int> totalWagers;
    int previousStreetsWager;
    Player playerToAct;
    ActionID lastAction;
    Street currentStreet;
};

struct HandData {
    HandRank rank;
    int index;

    auto operator<=>(const HandData&) const = default;
};

using SuitEquivalenceClass = FixedVector<Suit, 4>;

struct ChanceNodeInfo {
    CardSet availableCards;
    FixedVector<SuitEquivalenceClass, 4> isomorphisms;
};

struct SuitMapping {
    Suit child;
    Suit parent;

    bool operator==(const SuitMapping&) const = default;
};

// std::span doesn't have == for some reason...
template <typename T>
bool operator==(std::span<const T> lhs, std::span<const T> rhs) {
    return std::ranges::equal(lhs, rhs);
}

#endif // GAME_TYPES_HPP