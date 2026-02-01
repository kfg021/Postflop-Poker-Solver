#ifndef GAME_TYPES_HPP
#define GAME_TYPES_HPP

#include "game/holdem/config.hpp"
#include "util/fixed_vector.hpp"

#include <array>
#include <cstdint>

constexpr int StandardDeckSize = holdem::DeckSize;
constexpr int MaxNumDealCards = holdem::MaxNumDealCards;
constexpr int MaxNumActions = holdem::MaxNumActions;

using ActionID = std::uint8_t;
using CardID = std::uint8_t;
using CardSet = std::uint64_t;
using HandRank = std::uint32_t;

static constexpr CardID InvalidCard = 0xFF;

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

struct HandData {
    HandRank rank;
    int index;

    auto operator<=>(const HandData&) const = default;
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

private:
    constexpr int getPlayerID(Player player) const {
        int playerID = static_cast<int>(player);
        assert(playerID == 0 || playerID == 1);
        return playerID;
    }

    std::array<T, 2> m_array;
};

template <typename T>
class StreetArray {
public:
    constexpr StreetArray() = default;
    constexpr StreetArray(const T& flopValue, const T& turnValue, const T& riverValue) : m_array{ flopValue, turnValue, riverValue } {};

    constexpr const T& operator[](Street street) const {
        return m_array[getStreetID(street)];
    }

    constexpr T& operator[](Street street) {
        return m_array[getStreetID(street)];
    }

private:
    constexpr int getStreetID(Street street) const {
        int streetID = static_cast<int>(street);
        assert(streetID >= 0 && streetID <= 2);
        return streetID;
    }

    std::array<T, 3> m_array;
};

// TODO: Refactor to store the dealt chance cards directly?
struct GameState {
    CardSet currentBoard;
    PlayerArray<int> totalWagers;
    int previousStreetsWager;
    Player playerToAct;
    ActionID lastAction;
    CardID lastDealtCard;
    Street currentStreet;
};

using SuitEquivalenceClass = FixedVector<Suit, 4>;

struct SuitMapping {
    Suit child;
    Suit parent;

    bool operator==(const SuitMapping&) const = default;
};

#endif // GAME_TYPES_HPP