#include "trainer/train.hpp"

#include "game/game_types.hpp"
#include "solver/cfr.hpp"
#include "solver/tree.hpp"
#include "trainer/output.hpp"
#include "util/size_string.hpp"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {
struct CfrResult {
    float exploitability;
    int iteration;
};

std::optional<CfrResult> runCfr(const IGameRules& rules, float targetPercentExploitability, int maxIterations, int exploitabilityCheckFrequency, float startingPot, Tree& tree) {
    for (int i = 0; i < maxIterations; ++i) {
        int iteration = i + 1;

        for (Player hero : { Player::P0, Player::P1 }) {
            // Using Discounted CFR with alpha = 1.5, beta = 0, gamma = 2
            // These values work very well in practice, as shown in below paper

            // Brown, N., & Sandholm, T. (2019). 
            // Solving Imperfect-Information Games via Discounted Regret Minimization. 
            // Proceedings of the AAAI Conference on Artificial Intelligence, 33(01), 1829-1836. 
            // https://doi.org/10.1609/aaai.v33i01.33011829

            discountedCfr(hero, rules, getDiscountParams(1.5f, 0.0f, 2.0f, iteration), tree);
        }

        if ((exploitabilityCheckFrequency > 0) && (iteration % exploitabilityCheckFrequency == 0)) {
            float exploitability = calculateExploitabilityFast(rules, tree);
            float exploitabilityPercent = (exploitability / startingPot) * 100.0f;
            std::cout << "Finished iteration " << iteration << ". Exploitability: " << std::fixed << std::setprecision(5) << exploitability << " (" << exploitabilityPercent << "%)\n";
            if (exploitabilityPercent <= targetPercentExploitability) {
                return CfrResult{ exploitability, iteration };
            }
        }
    }

    return std::nullopt;
}
} // namespace

void train(const IGameRules& rules, float targetPercentExploitability, int maxIterations, int exploitabilityCheckFrequency, int numThreads, const std::optional<std::string>& strategyOutputFileOption) {
    assert(maxIterations > 0);

    Tree tree;

    std::cout << "Building tree...\n" << std::flush;

    tree.buildTreeSkeleton(rules);

    std::cout << "Finished building tree.\n";
    std::cout << "Total number of nodes: " << tree.allNodes.size() << "\n";
    std::cout << "Number of decision nodes: " << tree.getNumberOfDecisionNodes() << "\n";
    std::cout << "Static tree size: " << getSizeString(tree.getTreeSkeletonSize()) << "\n";
    std::cout << "Expected full tree size: " << getSizeString(tree.estimateFullTreeSize()) << "\n\n";

    std::cout << "Initializing tree...\n" << std::flush;
    tree.buildFullTree();
    std::cout << "Finished initializing tree.\n\n";

    GameState initialState = rules.getInitialGameState();
    float startingPot = initialState.totalWagers[Player::P0] + initialState.totalWagers[Player::P1] + tree.deadMoney;
    std::optional<CfrResult> resultOption;

    #ifdef _OPENMP
    omp_set_num_threads(numThreads);

    #pragma omp parallel
    {
        #pragma omp single
        {
            std::cout << "Starting training in parallel with " << omp_get_num_threads() << " threads. Target exploitability: "
                << std::fixed << std::setprecision(5) << targetPercentExploitability
                << "% Maximum iterations: " << maxIterations << "\n" << std::flush;

            resultOption = runCfr(rules, targetPercentExploitability, maxIterations, exploitabilityCheckFrequency, startingPot, tree);
        }
    }
    #else
    std::cout << "Starting training in single-threaded mode. Target exploitability: "
        << std::fixed << std::setprecision(5) << targetPercentExploitability
        << "% Maximum iterations: " << maxIterations << "\n" << std::flush;

    resultOption = runCfr(rules, targetPercentExploitability, maxIterations, exploitabilityCheckFrequency, startingPot, tree);
    #endif

    std::cout << "Finished training.\n";

    if (resultOption) {
        std::cout << "Target exploitability percentage reached after iteration " << resultOption->iteration << ".\n\n";
    }
    else {
        std::cout << "Target exploitability percentage not reached.\n\n";
    }

    std::cout << "Calculating expected value of final strategy...\n" << std::flush;
    float player0ExpectedValue = expectedValue(Player::P0, rules, tree);
    float player1ExpectedValue = expectedValue(Player::P1, rules, tree);
    std::cout << "Player 0 expected value: " << std::fixed << std::setprecision(5) << player0ExpectedValue << "\n";
    std::cout << "Player 1 expected value: " << std::fixed << std::setprecision(5) << player1ExpectedValue << "\n\n";

    std::cout << "Calculating exploitability of final strategy...\n" << std::flush;
    float exploitability = resultOption ? resultOption->exploitability : calculateExploitabilityFast(rules, tree);
    float exploitabilityPercent = (exploitability / startingPot) * 100.0f;
    std::cout << "Exploitability: " << std::fixed << std::setprecision(5) << exploitability << " (" << exploitabilityPercent << "%)\n\n";

    if (strategyOutputFileOption) {
        std::cout << "Saving strategy to file...\n" << std::flush;
        outputStrategyToJSON(rules, tree, *strategyOutputFileOption);
        std::cout << "Strategy saved to " << *strategyOutputFileOption << ".\n";
    }
}