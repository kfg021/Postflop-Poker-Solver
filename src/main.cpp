#include "game/kuhn_poker.hpp"
#include "game/leduc_poker.hpp"
#include "trainer/train.hpp"

#include <array>
#include <cstdint>

void trainKuhnPoker(int iterations, const std::string& strategyOutputFile) {
    KuhnPoker kuhnPokerRules;
    std::array<std::uint16_t, 2> RangeSizes = {3, 3}; // Jack, Queen, or King is possible for each player
    train(kuhnPokerRules, RangeSizes, iterations, strategyOutputFile);
}

void trainLeducPoker(int iterations, const std::string& strategyOutputFile) {
    LeducPoker leducPokerRules;
    std::array<std::uint16_t, 2> RangeSizes = {6, 6};
    train(leducPokerRules, RangeSizes, iterations, strategyOutputFile);
}

int main() {
    // trainKuhnPoker(100000, "kuhn_strategy.json");
    trainLeducPoker(100000, "leduc_strategy.json");
    return 0;
}