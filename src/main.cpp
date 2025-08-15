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
        buildRangeFromStrings({"AA", "KJ", "TT", "AQo:50"}).getValue(),
        buildRangeFromStrings({"AA", "KK:25", "QQ", "T9s:33", "27o:99"}).getValue(),
    };

    CardSet communityCards = buildCommunityCardsFromStrings({ "Ah", "7c", "2s", "3h", "7s" }).getValue();

    Holdem::Settings holdemSettings = {
        .ranges = ranges,
        .startingCommunityCards = communityCards,
        .betSizes = FixedVector<int, holdem::MaxNumBetSizes>{33, 100, 150},
        .raiseSizes = FixedVector<int, holdem::MaxNumRaiseSizes>{50, 100},
        .startingPlayerWagers = 12,
        .effectiveStackRemaining = 48,
        .deadMoney = 0
    };

    const int PrintFrequency = 100;

    std::cout << "Building Holdem lookup table...\n" << std::flush;
    hand_evaluation::buildLookupTablesIfNeeded();
    std::cout << "Finished building lookup table.\n\n";

    const Holdem holdemRules{ holdemSettings };
    train(holdemRules, iterations, PrintFrequency, strategyOutputFile);
}

int main() {
    trainKuhnPoker(100000, "kuhn_strategy.json");
    // trainLeducPoker(100000, "leduc_strategy.json");
    // trainHoldem(1000, "holdem_strategy.json");
    return 0;
}