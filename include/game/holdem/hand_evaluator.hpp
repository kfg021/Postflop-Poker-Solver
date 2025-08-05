#ifndef HAND_EVALUATOR_HPP
#define HAND_EVALUATOR_HPP

#include "game/game_types.hpp"
#include "util/fixed_vector.hpp"

#include <array>
#include <cstdint>

class HandEvaluator {
public:
    HandEvaluator();

    std::uint32_t getFiveCardHandRank(CardSet hand) const;
    std::uint32_t getSevenCardHandRank(CardSet hand) const;

private:
    static constexpr std::uint32_t HandRankTableSize = 2598960; // 52 choose 5
    using ChooseTable = std::array<std::array<std::uint32_t, 5>, 52>;
    using HandRankTable = std::array<std::uint32_t, HandRankTableSize>;
    
    static const ChooseTable& getChooseTable();
    static const HandRankTable& getHandRankTable();
    static std::uint32_t getFiveCardHandIndex(CardSet hand);
    static std::uint32_t generateFiveCardHandRank(CardSet hand);
};

#endif // HAND_EVALUATOR_HPP