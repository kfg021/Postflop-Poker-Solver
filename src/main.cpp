#include "game/kuhn_poker.hpp"
#include "game/leduc_poker.hpp"
#include "game/holdem/hand_evaluation.hpp"
#include "game/holdem/holdem.hpp"
#include "game/holdem/parse_input.hpp"
#include "trainer/train.hpp"

#include <iostream>
#include <string>
#include <vector>

void trainKuhnPoker(int iterations, const std::string& strategyOutputFile) {
    const KuhnPoker kuhnPokerRules;
    const int PrintFrequency = 0;
    train(kuhnPokerRules, iterations, PrintFrequency, strategyOutputFile);
}

void trainLeducPoker(int iterations, const std::string& strategyOutputFile) {
    const LeducPoker leducPokerRules;
    const int PrintFrequency = 1000;
    train(leducPokerRules, iterations, PrintFrequency, strategyOutputFile);
}

void trainHoldem(int iterations, const std::string& strategyOutputFile) {
    CardSet communityCards = buildCommunityCardsFromStrings({ "Ah", "7c", "2s", "3h" }).getValue();

    PlayerArray<Holdem::Range> ranges = {
        buildRangeFromStrings({"T9s", "44", "33", "22"}, communityCards).getValue(),
        buildRangeFromStrings({"AA", "AK", "AQs", "KQs"}, communityCards).getValue(),
    };

    Holdem::Settings holdemSettings = {
        .ranges = ranges,
        .startingCommunityCards = communityCards,
        .betSizes = FixedVector<int, holdem::MaxNumBetSizes>{33, 50},
        .raiseSizes = FixedVector<int, holdem::MaxNumRaiseSizes>{},
        .startingPlayerWagers = 100,
        .effectiveStackRemaining = 200,
        .deadMoney = 0,
    };

    const int PrintFrequency = 100;

    std::cout << "Building Holdem lookup tables...\n" << std::flush;
    const Holdem holdemRules{ holdemSettings };
    std::cout << "Finished building lookup tables.\n\n";

    train(holdemRules, iterations, PrintFrequency, strategyOutputFile);
}

int main() {
    // trainKuhnPoker(100000, "kuhn_strategy.json");
    // trainLeducPoker(10000, "leduc_strategy.json");
    trainHoldem(500, "holdem_strategy.json");
    return 0;
}