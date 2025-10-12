#include "solver/cfr.hpp"

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "solver/node.hpp"
#include "solver/tree.hpp"
#include "util/fixed_vector.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

// TODO: Do arithmetic with doubles, then store result in floats

namespace {
enum class TraversalMode : std::uint8_t {
    VanillaCfr,
    CfrPlus,
    DiscountedCfr,
    ExpectedValue,
    BestResponse
};

// TODO: Add range sizes to TraversalConstants
struct TraversalConstants {
    Player hero;
    PlayerArray<int> rangeSizes;
    TraversalMode mode;
    DiscountParams params;
};

std::size_t getExpectedValueIndex(int hand, std::size_t nodeIndex, const TraversalConstants& constants, const IGameRules& rules, const Tree& tree) {
    int heroRangeSize = tree.rangeSize[constants.hero];
    assert(hand < heroRangeSize);
    return nodeIndex * heroRangeSize + hand;
}

std::size_t getReachProbsIndex(int hand, std::size_t nodeIndex, const TraversalConstants& constants, const IGameRules& rules, const Tree& tree) {
    Player villain = getOpposingPlayer(constants.hero);
    int villainRangeSize = tree.rangeSize[villain];
    assert(hand < villainRangeSize);
    return nodeIndex * villainRangeSize + hand;
}

void writeCurrentStrategyToBuffer(const IGameRules& rules, const DecisionNode& decisionNode, Tree& tree) {
    int playerToActRangeSize = tree.rangeSize[decisionNode.player];
    int numActions = decisionNode.decisionDataSize;
    assert(numActions > 0);

    for (int hand = 0; hand < playerToActRangeSize; ++hand) {
        float totalPositiveRegret = 0.0f;

        for (int action = 0; action < numActions; ++action) {
            float regretSum = tree.allRegretSums[getTrainingDataIndex(action, hand, rules, decisionNode, tree)];
            if (regretSum > 0.0f) {
                totalPositiveRegret += regretSum;
            }
        }

        if (totalPositiveRegret == 0.0f) {
            // Uniform strategy
            for (int action = 0; action < numActions; ++action) {
                tree.allStrategies[getTrainingDataIndex(action, hand, rules, decisionNode, tree)] = 1.0f / numActions;
            }
        }
        else {
            for (int action = 0; action < numActions; ++action) {
                std::size_t trainingIndex = getTrainingDataIndex(action, hand, rules, decisionNode, tree);
                float regretSum = tree.allRegretSums[trainingIndex];
                if (regretSum > 0.0f) {
                    tree.allStrategies[trainingIndex] = regretSum / totalPositiveRegret;
                }
                else {
                    tree.allStrategies[trainingIndex] = 0.0f;
                }
            }
        }
    }
}

void traverseTree(
    const Node& node,
    std::size_t nodeIndex,
    const TraversalConstants& constants,
    const IGameRules& rules,
    std::vector<float>& allVillainReachProbs,
    std::vector<float>& allExpectedValues,
    Tree& tree
);

void traverseChance(
    const ChanceNode& chanceNode,
    std::size_t nodeIndex,
    const TraversalConstants& constants,
    const IGameRules& rules,
    std::vector<float>& allVillainReachProbs,
    std::vector<float>& allExpectedValues,
    Tree& tree
) {
    auto areHandAndCardDisjoint = [&rules, &tree](Player player, int hand, CardID card) -> bool {
        int playerRangeSize = tree.rangeSize[player];
        assert(hand < playerRangeSize);
        return !setContainsCard(rules.getRangeHands(player)[hand], card);
    };

    Player hero = constants.hero;
    Player villain = getOpposingPlayer(hero);

    int heroRangeSize = tree.rangeSize[hero];
    int villainRangeSize = tree.rangeSize[villain];

    int numChanceCards = chanceNode.chanceDataSize;

    // TODO: This should be a function in the rules interface
    const auto& heroRangeHands = rules.getRangeHands(constants.hero);
    assert(!heroRangeHands.empty());
    int gameHandSize = getSetSize(heroRangeHands[0]);

    // Normalize expected values by the number of total chance cards possible
    // Hero and villain both have a hand
    int chanceCardReachFactor = numChanceCards - (2 * gameHandSize);

    for (int cardIndex = 0; cardIndex < numChanceCards; ++cardIndex) {
        CardID chanceCard = tree.allChanceCards[chanceNode.chanceDataOffset + cardIndex];
        std::size_t nextNodeIndex = tree.allChanceNextNodeIndices[chanceNode.chanceDataOffset + cardIndex];
        assert(nextNodeIndex < tree.allNodes.size());
        const Node& nextNode = tree.allNodes[nextNodeIndex];

        // Update reach probabilities
        for (int hand = 0; hand < villainRangeSize; ++hand) {
            std::size_t oldReachIndex = getReachProbsIndex(hand, nodeIndex, constants, rules, tree);
            std::size_t newReachIndex = getReachProbsIndex(hand, nextNodeIndex, constants, rules, tree);

            if (areHandAndCardDisjoint(villain, hand, chanceCard)) {
                allVillainReachProbs[newReachIndex] = allVillainReachProbs[oldReachIndex] / static_cast<float>(chanceCardReachFactor);
            }
            else {
                allVillainReachProbs[newReachIndex] = 0.0f;
            }
        }

        traverseTree(nextNode, nextNodeIndex, constants, rules, allVillainReachProbs, allExpectedValues, tree);

        for (int hand = 0; hand < heroRangeSize; ++hand) {
            std::size_t currentNodeEVIndex = getExpectedValueIndex(hand, nodeIndex, constants, rules, tree);
            std::size_t nextNodeEVIndex = getExpectedValueIndex(hand, nextNodeIndex, constants, rules, tree);

            if (areHandAndCardDisjoint(hero, hand, chanceCard)) {
                allExpectedValues[currentNodeEVIndex] += allExpectedValues[nextNodeEVIndex];
            }
            else {
                assert(allExpectedValues[nextNodeEVIndex] == 0.0f);
            }
        }
    }
}

// TODO: Consider template for traverseDecision
void traverseDecision(
    const DecisionNode& decisionNode,
    std::size_t nodeIndex,
    const TraversalConstants& constants,
    const IGameRules& rules,
    std::vector<float>& allVillainReachProbs,
    std::vector<float>& allExpectedValues,
    Tree& tree
) {
    auto copyReachProbs = [&nodeIndex, &constants, &rules, &allVillainReachProbs, &tree](std::size_t nextNodeIndex) -> void {
        int villainRangeSize = tree.rangeSize[getOpposingPlayer(constants.hero)];
        for (int hand = 0; hand < villainRangeSize; ++hand) {
            std::size_t oldReachIndex = getReachProbsIndex(hand, nodeIndex, constants, rules, tree);
            std::size_t newReachIndex = getReachProbsIndex(hand, nextNodeIndex, constants, rules, tree);
            allVillainReachProbs[newReachIndex] = allVillainReachProbs[oldReachIndex];
        }
    };

    auto heroToActTraining = [
        &decisionNode,
        &nodeIndex,
        &constants,
        &rules,
        &allVillainReachProbs,
        &allExpectedValues,
        &tree,
        &copyReachProbs
    ]() {
        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        writeCurrentStrategyToBuffer(rules, decisionNode, tree);

        int heroRangeSize = tree.rangeSize[constants.hero];
        int villainRangeSize = tree.rangeSize[getOpposingPlayer(constants.hero)];

        // Regret and strategy discounting for DCFR
        if (constants.mode == TraversalMode::DiscountedCfr) {
            for (int action = 0; action < numActions; ++action) {
                for (int hand = 0; hand < heroRangeSize; ++hand) {
                    std::size_t trainingIndex = getTrainingDataIndex(action, hand, rules, decisionNode, tree);
                    float& regretSum = tree.allRegretSums[trainingIndex];
                    float& strategySum = tree.allStrategySums[trainingIndex];

                    if (regretSum > 0.0f) {
                        regretSum *= constants.params.alphaT;
                    }
                    else {
                        regretSum *= constants.params.betaT;
                    }

                    strategySum *= constants.params.gammaT;
                }
            }
        }

        for (int action = 0; action < numActions; ++action) {
            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            copyReachProbs(nextNodeIndex);

            traverseTree(nextNode, nextNodeIndex, constants, rules, allVillainReachProbs, allExpectedValues, tree);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                std::size_t currentNodeEVIndex = getExpectedValueIndex(hand, nodeIndex, constants, rules, tree);
                std::size_t nextNodeEVIndex = getExpectedValueIndex(hand, nextNodeIndex, constants, rules, tree);
                std::size_t trainingIndex = getTrainingDataIndex(action, hand, rules, decisionNode, tree);

                allExpectedValues[currentNodeEVIndex] += allExpectedValues[nextNodeEVIndex] * tree.allStrategies[trainingIndex];

                // Regret update part 1 - add EV of action
                float& regretSum = tree.allRegretSums[trainingIndex];
                regretSum += allExpectedValues[nextNodeEVIndex];
            }
        }

        // Regret update part 2 - subtract total EV
        // Strategy update
        for (int action = 0; action < numActions; ++action) {
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                std::size_t trainingIndex = getTrainingDataIndex(action, hand, rules, decisionNode, tree);
                float& regretSum = tree.allRegretSums[trainingIndex];
                float& strategySum = tree.allStrategySums[trainingIndex];

                regretSum -= allExpectedValues[getExpectedValueIndex(hand, nodeIndex, constants, rules, tree)];

                // TODO: Do we need to weight by hero reach probs?
                strategySum += tree.allStrategies[trainingIndex];

                // In CFR+, we erase negative regrets for faster convergence
                if (constants.mode == TraversalMode::CfrPlus) {
                    if (regretSum < 0.0f) {
                        regretSum = 0.0f;
                    }
                }
            }
        }
    };

    auto heroToActExpectedValue = [
        &decisionNode,
        &nodeIndex,
        &constants,
        &rules,
        &allVillainReachProbs,
        &allExpectedValues,
        &tree,
        &copyReachProbs
    ]() {
        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        writeAverageStrategyToBuffer(rules, decisionNode, tree);

        int heroRangeSize = tree.rangeSize[constants.hero];

        for (int action = 0; action < numActions; ++action) {
            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            copyReachProbs(nextNodeIndex);

            traverseTree(nextNode, nextNodeIndex, constants, rules, allVillainReachProbs, allExpectedValues, tree);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                std::size_t currentNodeEVIndex = getExpectedValueIndex(hand, nodeIndex, constants, rules, tree);
                std::size_t nextNodeEVIndex = getExpectedValueIndex(hand, nextNodeIndex, constants, rules, tree);
                std::size_t trainingIndex = getTrainingDataIndex(action, hand, rules, decisionNode, tree);
                allExpectedValues[currentNodeEVIndex] += allExpectedValues[nextNodeEVIndex] * tree.allStrategies[trainingIndex];
            }
        }
    };

    auto heroToActBestResponse = [
        &decisionNode,
        &nodeIndex,
        &constants,
        &rules,
        &allVillainReachProbs,
        &allExpectedValues,
        &tree,
        &copyReachProbs
    ]() -> void {
        // To calculate best response, hero plays the maximally exploitative pure strategy
        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        int heroRangeSize = tree.rangeSize[constants.hero];

        static constexpr float Infinity = std::numeric_limits<float>::infinity();

        // Initialize all EVs to lowest possible 
        for (int hand = 0; hand < heroRangeSize; ++hand) {
            allExpectedValues[getExpectedValueIndex(hand, nodeIndex, constants, rules, tree)] = -Infinity;
        }

        for (int action = 0; action < numActions; ++action) {
            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            copyReachProbs(nextNodeIndex);

            traverseTree(nextNode, nextNodeIndex, constants, rules, allVillainReachProbs, allExpectedValues, tree);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                std::size_t currentNodeEVIndex = getExpectedValueIndex(hand, nodeIndex, constants, rules, tree);
                std::size_t nextNodeEVIndex = getExpectedValueIndex(hand, nextNodeIndex, constants, rules, tree);

                // For each action, play the action that leads to highest EV
                allExpectedValues[currentNodeEVIndex] = std::max(allExpectedValues[currentNodeEVIndex], allExpectedValues[nextNodeEVIndex]);
            }
        }

        for (int hand = 0; hand < heroRangeSize; ++hand) {
            assert(allExpectedValues[getExpectedValueIndex(hand, nodeIndex, constants, rules, tree)] != -Infinity);
        }
    };

    auto villainToAct = [
        &decisionNode,
        &nodeIndex,
        &constants,
        &rules,
        &allVillainReachProbs,
        &allExpectedValues,
        &tree
    ]() -> void {
        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        Player villain = getOpposingPlayer(constants.hero);

        writeCurrentStrategyToBuffer(rules, decisionNode, tree);

        int heroRangeSize = tree.rangeSize[constants.hero];
        int villainRangeSize = tree.rangeSize[villain];

        // Not the hero's turn; no strategy or regret updates
        for (int action = 0; action < numActions; ++action) {
            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            // Update reach probabilities
            for (int hand = 0; hand < villainRangeSize; ++hand) {
                std::size_t oldReachIndex = getReachProbsIndex(hand, nodeIndex, constants, rules, tree);
                std::size_t newReachIndex = getReachProbsIndex(hand, nextNodeIndex, constants, rules, tree);
                std::size_t trainingIndex = getTrainingDataIndex(action, hand, rules, decisionNode, tree);
                allVillainReachProbs[newReachIndex] = allVillainReachProbs[oldReachIndex] * tree.allStrategies[trainingIndex];
            }

            traverseTree(nextNode, nextNodeIndex, constants, rules, allVillainReachProbs, allExpectedValues, tree);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                std::size_t currentNodeEVIndex = getExpectedValueIndex(hand, nodeIndex, constants, rules, tree);
                std::size_t nextNodeEVIndex = getExpectedValueIndex(hand, nextNodeIndex, constants, rules, tree);
                allExpectedValues[currentNodeEVIndex] += allExpectedValues[nextNodeEVIndex];
            }
        }
    };

    if (constants.hero == decisionNode.player) {
        switch (constants.mode) {
            case TraversalMode::VanillaCfr:
            case TraversalMode::CfrPlus:
            case TraversalMode::DiscountedCfr:
                heroToActTraining();
                break;
            case TraversalMode::ExpectedValue:
                heroToActExpectedValue();
                break;
            case TraversalMode::BestResponse:
                heroToActBestResponse();
                break;
            default:
                assert(false);
                break;
        }
    }
    else {
        villainToAct();
    }
}

void traverseFold(
    const FoldNode& foldNode,
    std::size_t nodeIndex,
    const TraversalConstants& constants,
    const IGameRules& rules,
    std::vector<float>& allVillainReachProbs,
    std::vector<float>& allExpectedValues,
    const Tree& tree
) {
    Player villain = getOpposingPlayer(constants.hero);

    int heroRangeSize = tree.rangeSize[constants.hero];
    int villainRangeSize = tree.rangeSize[villain];

    const auto& heroHands = rules.getRangeHands(constants.hero);
    const auto& villainHands = rules.getRangeHands(villain);

    int heroReward = (foldNode.remainingPlayer == constants.hero) ? foldNode.remainingPlayerReward : -foldNode.remainingPlayerReward;

    for (int i = 0; i < heroRangeSize; ++i) {
        if (!areSetsDisjoint(heroHands[i], foldNode.board)) continue;

        for (int j = 0; j < villainRangeSize; ++j) {
            if (!areSetsDisjoint(heroHands[i] | foldNode.board, villainHands[j])) continue;

            std::size_t expectedValueIndex = getExpectedValueIndex(i, nodeIndex, constants, rules, tree);
            std::size_t reachProbsIndex = getReachProbsIndex(j, nodeIndex, constants, rules, tree);
            allExpectedValues[expectedValueIndex] += static_cast<float>(heroReward) * allVillainReachProbs[reachProbsIndex];
        }
    }
}

void traverseShowdown(
    const ShowdownNode& showdownNode,
    std::size_t nodeIndex,
    const TraversalConstants& constants,
    const IGameRules& rules,
    std::vector<float>& allVillainReachProbs,
    std::vector<float>& allExpectedValues,
    Tree& tree
) {
    auto getMultiplier = [&showdownNode, &constants, &rules](int heroIndex, int villainIndex) -> int {
        PlayerArray<int> playerIndices = (constants.hero == Player::P0) ?
            PlayerArray<int>{ heroIndex, villainIndex } :
            PlayerArray<int>{ villainIndex, heroIndex };

        switch (rules.getShowdownResult(playerIndices, showdownNode.board)) {
            case ShowdownResult::P0Win:
                return (constants.hero == Player::P0) ? 1 : -1;
            case ShowdownResult::P1Win:
                return (constants.hero == Player::P1) ? 1 : -1;
            case ShowdownResult::Tie:
                return 0;
            default:
                assert(false);
                return 0;
        }
    };

    Player villain = getOpposingPlayer(constants.hero);

    int heroRangeSize = tree.rangeSize[constants.hero];
    int villainRangeSize = tree.rangeSize[villain];

    const auto& heroHands = rules.getRangeHands(constants.hero);
    const auto& villainHands = rules.getRangeHands(villain);

    for (int i = 0; i < heroRangeSize; ++i) {
        if (!areSetsDisjoint(heroHands[i], showdownNode.board)) continue;

        for (int j = 0; j < villainRangeSize; ++j) {
            if (!areSetsDisjoint(heroHands[i] | showdownNode.board, villainHands[j])) continue;

            float heroReward = static_cast<float>(getMultiplier(i, j)) * static_cast<float>(showdownNode.reward);
            std::size_t expectedValueIndex = getExpectedValueIndex(i, nodeIndex, constants, rules, tree);
            std::size_t reachProbsIndex = getReachProbsIndex(j, nodeIndex, constants, rules, tree);
            allExpectedValues[expectedValueIndex] += heroReward * allVillainReachProbs[reachProbsIndex];
        }
    }
}

void traverseTree(
    const Node& node,
    std::size_t nodeIndex,
    const TraversalConstants& constants,
    const IGameRules& rules,
    std::vector<float>& allVillainReachProbs,
    std::vector<float>& allExpectedValues,
    Tree& tree
) {
    assert(tree.isTreeSkeletonBuilt() && tree.isFullTreeBuilt());

    switch (node.getNodeType()) {
        case NodeType::Chance:
            traverseChance(node.chanceNode, nodeIndex, constants, rules, allVillainReachProbs, allExpectedValues, tree);
            break;
        case NodeType::Decision:
            traverseDecision(node.decisionNode, nodeIndex, constants, rules, allVillainReachProbs, allExpectedValues, tree);
            break;
        case NodeType::Fold:
            traverseFold(node.foldNode, nodeIndex, constants, rules, allVillainReachProbs, allExpectedValues, tree);
            break;
        case NodeType::Showdown:
            traverseShowdown(node.showdownNode, nodeIndex, constants, rules, allVillainReachProbs, allExpectedValues, tree);
            break;
        default:
            assert(false);
            break;
    }
}

void traverseFromRoot(const TraversalConstants& constants, const IGameRules& rules, Tree& tree) {
    std::size_t rootNodeIndex = tree.getRootNodeIndex();

    Player villain = getOpposingPlayer(constants.hero);

    std::vector<float>& allVillainReachProbs = tree.allInputOutput[villain];
    std::vector<float>& allExpectedValues = tree.allInputOutput[constants.hero];

    // Reset temporary vectors
    std::fill(tree.allStrategies.begin(), tree.allStrategies.end(), 0.0f);
    std::fill(allVillainReachProbs.begin(), allVillainReachProbs.end(), 0.0f);
    std::fill(allExpectedValues.begin(), allExpectedValues.end(), 0.0f);

    const auto& initialRangeWeights = rules.getInitialRangeWeights(villain);

    int villainRangeSize = tree.rangeSize[villain];
    for (int hand = 0; hand < villainRangeSize; ++hand) {
        std::size_t reachIndex = getReachProbsIndex(hand, rootNodeIndex, constants, rules, tree);
        allVillainReachProbs[reachIndex] = initialRangeWeights[hand];
    }

    return traverseTree(
        tree.allNodes[rootNodeIndex],
        rootNodeIndex,
        constants,
        rules,
        allVillainReachProbs,
        allExpectedValues,
        tree
    );
}

float rootExpectedValue(
    Player hero,
    const IGameRules& rules,
    Tree& tree,
    TraversalMode mode
) {
    auto getTotalRangeWeight = [&hero, &rules]() -> float {
        Player villain = getOpposingPlayer(hero);

        const auto& heroRangeWeights = rules.getInitialRangeWeights(hero);
        const auto& villainRangeWeights = rules.getInitialRangeWeights(villain);

        int heroRangeSize = heroRangeWeights.size();
        int villainRangeSize = villainRangeWeights.size();

        const auto& heroHands = rules.getRangeHands(hero);
        const auto& villainHands = rules.getRangeHands(villain);

        CardSet startingBoard = rules.getInitialGameState().currentBoard;

        float totalRangeWeight = 0.0f;

        for (int i = 0; i < heroRangeSize; ++i) {
            if (!areSetsDisjoint(heroHands[i], startingBoard)) continue;

            for (int j = 0; j < villainRangeSize; ++j) {
                if (!areSetsDisjoint(heroHands[i] | startingBoard, villainHands[j])) continue;

                totalRangeWeight += heroRangeWeights[i] * villainRangeWeights[j];
            }
        }

        return totalRangeWeight;
    };

    assert((mode == TraversalMode::ExpectedValue) || (mode == TraversalMode::BestResponse));

    TraversalConstants constants = {
       .hero = hero,
       .mode = mode,
       .params = {} // No params needed for expected value
    };

    traverseFromRoot(constants, rules, tree);

    const auto& heroRangeWeights = rules.getInitialRangeWeights(hero);

    std::size_t rootNodeIndex = tree.getRootNodeIndex();
    const std::vector<float>& allExpectedValues = tree.allInputOutput[constants.hero];

    int heroRangeSize = tree.rangeSize[hero];
    float expectedValue = 0.0f;
    for (int hand = 0; hand < heroRangeSize; ++hand) {
        expectedValue += allExpectedValues[getExpectedValueIndex(hand, rootNodeIndex, constants, rules, tree)] * heroRangeWeights[hand];
    }

    // TODO: Check if totalRangeWeight is 0 before we even build the tree
    float totalRangeWeight = getTotalRangeWeight();
    assert(totalRangeWeight > 0.0f);
    expectedValue /= totalRangeWeight;

    return expectedValue;
}
} // namespace

DiscountParams getDiscountParams(float alpha, float beta, float gamma, int iteration) {
    float t = static_cast<float>(iteration + 1);
    float a = std::pow(t, alpha);
    float b = std::pow(t, beta);

    return {
        .alphaT = a / (a + 1),
        .betaT = b / (b + 1),
        .gammaT = std::pow(t / (t + 1), gamma)
    };
}

void vanillaCfr(
    Player hero,
    const IGameRules& rules,
    Tree& tree
) {
    TraversalConstants constants = {
        .hero = hero,
        .mode = TraversalMode::VanillaCfr,
        .params = {} // No params needed for vanilla CFR
    };

    traverseFromRoot(constants, rules, tree);
}

void cfrPlus(
    Player hero,
    const IGameRules& rules,
    Tree& tree
) {
    TraversalConstants constants = {
        .hero = hero,
        .mode = TraversalMode::CfrPlus,
        .params = {} // No params needed for CFR+
    };

    traverseFromRoot(constants, rules, tree);
}

void discountedCfr(
    Player hero,
    const IGameRules& rules,
    const DiscountParams& params,
    Tree& tree
) {
    TraversalConstants constants = {
        .hero = hero,
        .mode = TraversalMode::DiscountedCfr,
        .params = params
    };

    traverseFromRoot(constants, rules, tree);
}

float expectedValue(
    Player hero,
    const IGameRules& rules,
    Tree& tree
) {
    return rootExpectedValue(hero, rules, tree, TraversalMode::ExpectedValue);
}

float bestResponseEV(
    Player hero,
    const IGameRules& rules,
    Tree& tree
) {
    return rootExpectedValue(hero, rules, tree, TraversalMode::BestResponse);
}

void writeAverageStrategyToBuffer(const IGameRules& rules, const DecisionNode& decisionNode, Tree& tree) {
    int playerToActRangeSize = tree.rangeSize[decisionNode.player];
    int numActions = decisionNode.decisionDataSize;
    assert(numActions > 0);

    for (int hand = 0; hand < playerToActRangeSize; ++hand) {
        float total = 0.0f;
        for (int action = 0; action < numActions; ++action) {
            float strategySum = tree.allStrategySums[getTrainingDataIndex(action, hand, rules, decisionNode, tree)];
            total += strategySum;
        }

        if (total == 0.0f) {
            // Uniform strategy
            for (int action = 0; action < numActions; ++action) {
                tree.allStrategies[getTrainingDataIndex(action, hand, rules, decisionNode, tree)] = 1.0f / numActions;
            }
        }
        else {
            for (int action = 0; action < numActions; ++action) {
                std::size_t trainingIndex = getTrainingDataIndex(action, hand, rules, decisionNode, tree);
                float strategySum = tree.allStrategySums[trainingIndex];
                tree.allStrategies[trainingIndex] = strategySum / total;
            }
        }
    }
}

std::size_t getTrainingDataIndex(int action, int hand, const IGameRules& rules, const DecisionNode& decisionNode, const Tree& tree) {
    assert(action >= 0 && action < decisionNode.decisionDataSize);

    int playerToActRangeSize = tree.rangeSize[decisionNode.player];
    assert(hand >= 0 && hand < playerToActRangeSize);

    return decisionNode.trainingDataOffset + (action * playerToActRangeSize) + hand;
}