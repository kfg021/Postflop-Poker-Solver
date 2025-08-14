#include "trainer/train.hpp"

#include "game/game_types.hpp"
#include "solver/cfr.hpp"
#include "solver/tree.hpp"
#include "trainer/output.hpp"
#include "util/size_string.hpp"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

void train(const IGameRules& rules, int iterations, int printFrequency, const std::string& strategyOutputFile) {
    assert(iterations > 0);

    Tree tree;

    std::cout << "Building tree...\n" << std::flush;

    tree.buildTreeSkeleton(rules);

    std::cout << "Finished building tree.\n";
    std::cout << "Number of decision nodes: " << tree.getNumberOfDecisionNodes() << "\n";
    std::cout << "Total number of nodes: " << tree.allNodes.size() << "\n";
    std::cout << "Static tree size: " << getSizeString(tree.getTreeSkeletonSize()) << "\n";
    std::cout << "Expected full tree size: " << getSizeString(tree.estimateFullTreeSize()) << "\n\n";

    std::cout << "Initializing tree...\n" << std::flush;
    tree.buildFullTree();
    std::cout << "Finished initializing tree.\n\n";

    std::cout << "Training for " << iterations << " iterations...\n" << std::flush;
    std::vector<InitialSetup> initialSetups = rules.getInitialSetups();

    float player0ExpectedValueSum = 0.0f;
    for (int i = 0; i < iterations; ++i) {
        for (const InitialSetup& setup : initialSetups) {
            float cfrResult = cfr(rules, setup.handIndices, setup.weights, tree.getRootNode(), tree);
            player0ExpectedValueSum += setup.matchupProbability * cfrResult;
        }

        if ((printFrequency > 0) && (i % printFrequency) == 0) {
            std::cout << "Finished iteration " << i << "\n";
        }
    }

    std::cout << "Finished training.\n";
    std::cout << "Player 0 expected value: " << std::fixed << std::setprecision(5) << player0ExpectedValueSum / iterations << "\n\n";

    std::cout << "Saving strategy to file...\n" << std::flush;
    outputStrategyToJSON(rules, tree, strategyOutputFile);
    std::cout << "Strategy saved to " << strategyOutputFile << ".\n";
}