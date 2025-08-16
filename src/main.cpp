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
    PlayerArray<std::vector<Holdem::RangeElement>> ranges = {
        buildRangeFromStrings({"T9s", "44", "33", "22"}).getValue(),
        buildRangeFromStrings({"AA", "AK", "AQs", "KQs"}).getValue(),
    };

    CardSet communityCards = buildCommunityCardsFromStrings({ "Ah", "7c", "2s", "3h" }).getValue();

    Holdem::Settings holdemSettings = {
        .ranges = ranges,
        .startingCommunityCards = communityCards,
        .betSizes = FixedVector<int, holdem::MaxNumBetSizes>{33, 50},
        .raiseSizes = FixedVector<int, holdem::MaxNumRaiseSizes>{},
        .startingPlayerWagers = 100,
        .effectiveStackRemaining = 200,
        .deadMoney = 0,
    };

    const int PrintFrequency = 1;

    std::cout << "Building Holdem lookup table...\n" << std::flush;
    hand_evaluation::buildLookupTablesIfNeeded();
    std::cout << "Finished building lookup table.\n\n";

    const Holdem holdemRules{ holdemSettings };
    train(holdemRules, iterations, PrintFrequency, strategyOutputFile);
}

int main() {
    // trainKuhnPoker(100000, "kuhn_strategy.json");
    // trainLeducPoker(100000, "leduc_strategy.json");
    trainHoldem(500, "holdem_strategy.json");
    return 0;
}