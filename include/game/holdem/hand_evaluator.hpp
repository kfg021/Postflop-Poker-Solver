#ifndef HAND_EVALUATOR_HPP
#define HAND_EVALUATOR_HPP

#include "game/game_types.hpp"

#include <array>

class HandEvaluator {
public:
    ShowdownResult getShowdownResult(const std::array<CardSet, 2>& playerHands, CardSet board) const;

private:
    constexpr static int HandRankTableSize = 2598960; // 52 choose 5
    using ChooseTable = std::array<std::array<std::uint32_t, 5>, 52>;
    using HandRankTable = std::array<std::uint32_t, HandRankTableSize>;

    static const ChooseTable& getChooseTable();
    static const HandRankTable& getHandRankTable();
    static std::uint32_t getFiveCardHandIndex(CardSet hand);

    std::uint32_t getSevenCardHandRank(CardSet sevenCards) const;
};

#endif // HAND_EVALUATOR_HPP