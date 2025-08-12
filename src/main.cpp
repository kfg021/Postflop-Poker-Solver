#include "game/kuhn_poker.hpp"
#include "game/leduc_poker.hpp"
#include "trainer/train.hpp"

void trainKuhnPoker(int iterations, const std::string& strategyOutputFile) {
    const KuhnPoker kuhnPokerRules;
    train(kuhnPokerRules, iterations, strategyOutputFile);
}

void trainLeducPoker(int iterations, const std::string& strategyOutputFile) {
    const LeducPoker leducPokerRules;
    train(leducPokerRules, iterations, strategyOutputFile);
}

int main() {
    // trainKuhnPoker(100000, "kuhn_strategy.json");
    trainLeducPoker(100000, "leduc_strategy.json");
    return 0;
}