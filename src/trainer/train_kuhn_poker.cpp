#include "trainer/train_kuhn_poker.hpp"

#include "game/game_types.hpp"
#include "game/kuhn_poker.hpp"
#include "io/output.hpp"
#include "solver/cfr.hpp"
#include "solver/tree.hpp"
#include "util/size_string.hpp"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <vector>

void trainKuhnPoker(int iterations) {
    assert(iterations > 0);

    KuhnPoker kuhnPokerRules;
    Tree tree;

    std::cout << "Building tree...\n" << std::flush;

    static constexpr std::array<std::uint16_t, 2> RangeSizes = { 3, 3 }; // Jack, Queen, or King is possible for each player
    tree.buildTreeSkeleton(kuhnPokerRules, RangeSizes);

    std::cout << "Finished building tree.\n";
    std::cout << "Number of nodes: " << tree.allNodes.size() << "\n";
    std::cout << "Static tree size: " << getSizeString(tree.getTreeSkeletonSize()) << "\n";
    std::cout << "Expected full tree size: " << getSizeString(tree.estimateFullTreeSize()) << "\n\n" << std::flush;

    std::cout << "Initializing tree...\n" << std::flush;
    tree.buildFullTree();
    std::cout << "Finished initializing tree.\n\n" << std::flush;

    std::cout << "Training for " << iterations << " iterations...\n" << std::flush;
    std::vector<InitialSetup> initialSetups = kuhnPokerRules.getInitialSetups();

    float player0ExpectedValueSum = 0.0f;
    for (int i = 0; i < iterations; ++i) {
        for (const InitialSetup& setup : initialSetups) {
            float cfrResult = cfr(kuhnPokerRules, setup.playerHands, setup.playerWeights, tree.getRootNode(), tree);
            player0ExpectedValueSum += setup.matchupProbability * cfrResult;
        }
    }

    std::cout << "Finished training.\n";
    std::cout << "Player 0 expected value: " << std::fixed << std::setprecision(5) << player0ExpectedValueSum / iterations << "\n\n";

    outputStrategyToJSON(kuhnPokerRules, tree, "output.json");
}