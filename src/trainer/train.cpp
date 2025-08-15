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

    for (int i = 0; i < iterations; ++i) {
        for (Player traverser : { Player::P0, Player::P1 }) {
            for (const InitialSetup& setup : initialSetups) {
                cfrPlus(rules, traverser, setup.handIndices, setup.weights, tree.getRootNode(), tree);
            }
        }

        if ((printFrequency > 0) && (i % printFrequency) == 0) {
            std::cout << "Finished iteration " << i << "\n";
        }
    }
    std::cout << "Finished training.\n\n";

    std::cout << "Calculating expected value of final strategy:\n" << std::flush;
    float player0ExpectedValue = 0.0f;
    for (const InitialSetup& setup : initialSetups) {
        float setupExpectedValue = calculatePlayerExpectedValue(rules, Player::P0, setup.handIndices, setup.weights, tree.getRootNode(), tree);
        player0ExpectedValue += setup.matchupProbability * setupExpectedValue;
    }
    std::cout << "Player 0 expected value: " << std::fixed << std::setprecision(5) << player0ExpectedValue << "\n\n";

    std::cout << "Calculating exploitability of final strategy:\n" << std::flush;
    float totalExploitability = 0.0f;
    for (Player player : { Player::P0, Player::P1 }) {
        float playerBestResponse = 0.0f;
        for (const InitialSetup& setup : initialSetups) {
            float setupBestResponseP0Perspective = calculatePlayerBestResponse(rules, player, setup.handIndices, setup.weights, tree.getRootNode(), tree);
            float setupBestResponse = (player == Player::P0) ? setupBestResponseP0Perspective : -setupBestResponseP0Perspective;
            playerBestResponse += setup.matchupProbability * setupBestResponse;
        }

        float playerExpectedValue = (player == Player::P0) ? player0ExpectedValue : -player0ExpectedValue;
        float playerExploitability = playerBestResponse - playerExpectedValue;
        std::cout << "Player " << ((player == Player::P0) ? 0 : 1) << " exploitability: " << std::fixed << std::setprecision(5) << playerExploitability << "\n";

        totalExploitability += playerExploitability;
    }
    std::cout << "Total exploitability: " << std::fixed << std::setprecision(5) << (totalExploitability / 2) << "\n\n";


    std::cout << "Saving strategy to file...\n" << std::flush;
    outputStrategyToJSON(rules, tree, strategyOutputFile);
    std::cout << "Strategy saved to " << strategyOutputFile << ".\n";
}