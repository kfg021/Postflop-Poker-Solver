#include "game/kuhn_poker.hpp"
#include "game/leduc_poker.hpp"
#include "game/holdem/hand_evaluation.hpp"
#include "game/holdem/holdem.hpp"
#include "game/holdem/parse_input.hpp"
#include "trainer/train.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {
void trainKuhnPoker(int maxIterations, const std::optional<std::string>& strategyOutputFileOption) {
    const KuhnPoker kuhnPokerRules;

    static constexpr float TargetExploitabilityPercent = 0.3f;
    static constexpr int ExploitabilityCheckFrequency = 10000;

    train(kuhnPokerRules, TargetExploitabilityPercent, maxIterations, ExploitabilityCheckFrequency, strategyOutputFileOption);
}

void trainLeducPoker(int maxIterations, const std::optional<std::string>& strategyOutputFileOption) {
    const LeducPoker leducPokerRules;

    static constexpr float TargetExploitabilityPercent = 0.3f;
    static constexpr int ExploitabilityCheckFrequency = 1000;

    train(leducPokerRules, TargetExploitabilityPercent, maxIterations, ExploitabilityCheckFrequency, strategyOutputFileOption);
}

void trainHoldem(int maxIterations, const std::optional<std::string>& strategyOutputFileOption) {
    CardSet communityCards = buildCommunityCardsFromStrings({ "9s", "8h", "3s" }).getValue();

    PlayerArray<Holdem::Range> ranges = {
        buildRangeFromStrings({ "AA", "KK", "QQ", "AK", "AQs", "A5s" }, communityCards).getValue(),
        buildRangeFromStrings({ "QQ", "JJ", "TT", "99", "AKo", "AQ", "AJs", "ATs", "KQs", "KJs", "KTs", "QJs", "JTs", "T9s" }, communityCards).getValue(),
    };

    Holdem::Settings holdemSettings = {
        .ranges = ranges,
        .startingCommunityCards = communityCards,
        .betSizes = FixedVector<int, holdem::MaxNumBetSizes>{50},
        .raiseSizes = FixedVector<int, holdem::MaxNumRaiseSizes>{},
        .startingPlayerWagers = 50,
        .effectiveStackRemaining = 100,
        .deadMoney = 0,
    };

    static constexpr float TargetExploitabilityPercent = 0.3f;
    static constexpr int ExploitabilityCheckFrequency = 10;

    std::cout << "Building Holdem lookup tables...\n" << std::flush;
    const Holdem holdemRules{ holdemSettings };
    std::cout << "Finished building lookup tables.\n\n";

    train(holdemRules, TargetExploitabilityPercent, maxIterations, ExploitabilityCheckFrequency, strategyOutputFileOption);
}
} // namespace

int main() {
    // trainKuhnPoker(100000, "kuhn_strategy.json");
    // trainLeducPoker(10000, "leduc_strategy.json");
    trainHoldem(100, std::nullopt);
    return 0;
}