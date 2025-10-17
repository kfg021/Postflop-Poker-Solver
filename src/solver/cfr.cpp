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
#include <ranges>
#include <span>
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

struct TraversalConstants {
    Player hero;
    TraversalMode mode;
    DiscountParams params;
};

std::size_t getExpectedValueIndex(int hand, std::size_t nodeIndex, const TraversalConstants& constants, const Tree& tree) {
    int heroRangeSize = tree.rangeSize[constants.hero];
    assert(hand < heroRangeSize);
    return nodeIndex * heroRangeSize + hand;
}

std::size_t getReachProbsIndex(int hand, std::size_t nodeIndex, const TraversalConstants& constants, const Tree& tree) {
    Player villain = getOpposingPlayer(constants.hero);
    int villainRangeSize = tree.rangeSize[villain];
    assert(hand < villainRangeSize);
    return nodeIndex * villainRangeSize + hand;
}

void writeCurrentStrategyToBuffer(const DecisionNode& decisionNode, Tree& tree) {
    int playerToActRangeSize = tree.rangeSize[decisionNode.player];
    int numActions = decisionNode.decisionDataSize;
    assert(numActions > 0);

    for (int hand = 0; hand < playerToActRangeSize; ++hand) {
        float totalPositiveRegret = 0.0f;

        for (int action = 0; action < numActions; ++action) {
            float regretSum = tree.allRegretSums[getTrainingDataIndex(action, hand, decisionNode, tree)];
            if (regretSum > 0.0f) {
                totalPositiveRegret += regretSum;
            }
        }

        if (totalPositiveRegret == 0.0f) {
            // Uniform strategy
            for (int action = 0; action < numActions; ++action) {
                tree.allStrategies[getTrainingDataIndex(action, hand, decisionNode, tree)] = 1.0f / numActions;
            }
        }
        else {
            for (int action = 0; action < numActions; ++action) {
                std::size_t trainingIndex = getTrainingDataIndex(action, hand, decisionNode, tree);
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

float getValidVillainReachProb(
    int heroHandIndex,
    float villainTotalReachProb,
    std::size_t nodeIndex,
    const std::array<float, StandardDeckSize>& villainReachProbWithCard,
    const TraversalConstants& constants,
    const std::vector<float>& allVillainReachProbs,
    const Tree& tree
) {
    CardSet heroHand = tree.rangeHands[constants.hero][heroHandIndex];
    float villainValidReachProb = villainTotalReachProb;

    switch (tree.gameHandSize) {
        case 1:
            villainValidReachProb -= villainReachProbWithCard[getLowestCardInSet(heroHand)];
            break;
        case 2: {
            villainValidReachProb -= villainReachProbWithCard[popLowestCardFromSet(heroHand)];
            villainValidReachProb -= villainReachProbWithCard[popLowestCardFromSet(heroHand)];
            assert(heroHand == 0);

            // Inclusion-Exclusion: Add back the portion of the villain's range that was double subtracted by the above
            int sameHandIndex = tree.sameHandIndexTable[constants.hero][heroHandIndex];
            if (sameHandIndex != -1) {
                villainValidReachProb += allVillainReachProbs[getReachProbsIndex(sameHandIndex, nodeIndex, constants, tree)];
            }
            break;
        }
        default:
            // For hand size > 2, logic is more complicated
            assert(false);
            break;
    }

    return villainValidReachProb;
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
    auto areHandAndCardDisjoint = [&tree](Player player, int hand, CardID card) -> bool {
        assert(hand < tree.rangeSize[player]);
        return !setContainsCard(tree.rangeHands[player][hand], card);
    };

    Player hero = constants.hero;
    Player villain = getOpposingPlayer(hero);

    int heroRangeSize = tree.rangeSize[hero];
    int villainRangeSize = tree.rangeSize[villain];

    int numChanceCards = chanceNode.chanceDataSize;

    // Normalize expected values by the number of total chance cards possible
    // Hero and villain both have a hand
    int chanceCardReachFactor = numChanceCards - (2 * tree.gameHandSize);

    for (int cardIndex = 0; cardIndex < numChanceCards; ++cardIndex) {
        CardID chanceCard = tree.allChanceCards[chanceNode.chanceDataOffset + cardIndex];
        std::size_t nextNodeIndex = tree.allChanceNextNodeIndices[chanceNode.chanceDataOffset + cardIndex];
        assert(nextNodeIndex < tree.allNodes.size());
        const Node& nextNode = tree.allNodes[nextNodeIndex];

        // Update reach probabilities
        for (int hand = 0; hand < villainRangeSize; ++hand) {
            std::size_t oldReachIndex = getReachProbsIndex(hand, nodeIndex, constants, tree);
            std::size_t newReachIndex = getReachProbsIndex(hand, nextNodeIndex, constants, tree);

            if (areHandAndCardDisjoint(villain, hand, chanceCard)) {
                allVillainReachProbs[newReachIndex] = allVillainReachProbs[oldReachIndex] / static_cast<float>(chanceCardReachFactor);
            }
            else {
                allVillainReachProbs[newReachIndex] = 0.0f;
            }
        }

        traverseTree(nextNode, nextNodeIndex, constants, rules, allVillainReachProbs, allExpectedValues, tree);

        for (int hand = 0; hand < heroRangeSize; ++hand) {
            std::size_t currentNodeEVIndex = getExpectedValueIndex(hand, nodeIndex, constants, tree);
            std::size_t nextNodeEVIndex = getExpectedValueIndex(hand, nextNodeIndex, constants, tree);

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
    auto copyReachProbs = [&nodeIndex, &constants, &allVillainReachProbs, &tree](std::size_t nextNodeIndex) -> void {
        int villainRangeSize = tree.rangeSize[getOpposingPlayer(constants.hero)];
        for (int hand = 0; hand < villainRangeSize; ++hand) {
            std::size_t oldReachIndex = getReachProbsIndex(hand, nodeIndex, constants, tree);
            std::size_t newReachIndex = getReachProbsIndex(hand, nextNodeIndex, constants, tree);
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

        writeCurrentStrategyToBuffer(decisionNode, tree);

        int heroRangeSize = tree.rangeSize[constants.hero];
        int villainRangeSize = tree.rangeSize[getOpposingPlayer(constants.hero)];

        // Regret and strategy discounting for DCFR
        if (constants.mode == TraversalMode::DiscountedCfr) {
            for (int action = 0; action < numActions; ++action) {
                for (int hand = 0; hand < heroRangeSize; ++hand) {
                    std::size_t trainingIndex = getTrainingDataIndex(action, hand, decisionNode, tree);
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
                std::size_t currentNodeEVIndex = getExpectedValueIndex(hand, nodeIndex, constants, tree);
                std::size_t nextNodeEVIndex = getExpectedValueIndex(hand, nextNodeIndex, constants, tree);
                std::size_t trainingIndex = getTrainingDataIndex(action, hand, decisionNode, tree);

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
                std::size_t trainingIndex = getTrainingDataIndex(action, hand, decisionNode, tree);
                float& regretSum = tree.allRegretSums[trainingIndex];
                float& strategySum = tree.allStrategySums[trainingIndex];

                regretSum -= allExpectedValues[getExpectedValueIndex(hand, nodeIndex, constants, tree)];

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

        writeAverageStrategyToBuffer(decisionNode, tree);

        int heroRangeSize = tree.rangeSize[constants.hero];

        for (int action = 0; action < numActions; ++action) {
            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            copyReachProbs(nextNodeIndex);

            traverseTree(nextNode, nextNodeIndex, constants, rules, allVillainReachProbs, allExpectedValues, tree);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                std::size_t currentNodeEVIndex = getExpectedValueIndex(hand, nodeIndex, constants, tree);
                std::size_t nextNodeEVIndex = getExpectedValueIndex(hand, nextNodeIndex, constants, tree);
                std::size_t trainingIndex = getTrainingDataIndex(action, hand, decisionNode, tree);
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
            allExpectedValues[getExpectedValueIndex(hand, nodeIndex, constants, tree)] = -Infinity;
        }

        for (int action = 0; action < numActions; ++action) {
            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            copyReachProbs(nextNodeIndex);

            traverseTree(nextNode, nextNodeIndex, constants, rules, allVillainReachProbs, allExpectedValues, tree);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                std::size_t currentNodeEVIndex = getExpectedValueIndex(hand, nodeIndex, constants, tree);
                std::size_t nextNodeEVIndex = getExpectedValueIndex(hand, nextNodeIndex, constants, tree);

                // For each action, play the action that leads to highest EV
                allExpectedValues[currentNodeEVIndex] = std::max(allExpectedValues[currentNodeEVIndex], allExpectedValues[nextNodeEVIndex]);
            }
        }

        for (int hand = 0; hand < heroRangeSize; ++hand) {
            assert(allExpectedValues[getExpectedValueIndex(hand, nodeIndex, constants, tree)] != -Infinity);
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

        writeCurrentStrategyToBuffer(decisionNode, tree);

        int heroRangeSize = tree.rangeSize[constants.hero];
        int villainRangeSize = tree.rangeSize[villain];

        // Not the hero's turn; no strategy or regret updates
        for (int action = 0; action < numActions; ++action) {
            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            // Update reach probabilities
            for (int hand = 0; hand < villainRangeSize; ++hand) {
                std::size_t oldReachIndex = getReachProbsIndex(hand, nodeIndex, constants, tree);
                std::size_t newReachIndex = getReachProbsIndex(hand, nextNodeIndex, constants, tree);
                std::size_t trainingIndex = getTrainingDataIndex(action, hand, decisionNode, tree);
                allVillainReachProbs[newReachIndex] = allVillainReachProbs[oldReachIndex] * tree.allStrategies[trainingIndex];
            }

            traverseTree(nextNode, nextNodeIndex, constants, rules, allVillainReachProbs, allExpectedValues, tree);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                std::size_t currentNodeEVIndex = getExpectedValueIndex(hand, nodeIndex, constants, tree);
                std::size_t nextNodeEVIndex = getExpectedValueIndex(hand, nextNodeIndex, constants, tree);
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
    std::vector<float>& allVillainReachProbs,
    std::vector<float>& allExpectedValues,
    const Tree& tree
) {
    Player villain = getOpposingPlayer(constants.hero);

    int heroRangeSize = tree.rangeSize[constants.hero];
    int villainRangeSize = tree.rangeSize[villain];

    float villainTotalReachProb = 0.0f;
    std::array<float, StandardDeckSize> villainReachProbWithCard = {};

    for (int hand = 0; hand < villainRangeSize; ++hand) {
        CardSet villainHand = tree.rangeHands[villain][hand];
        if (!areSetsDisjoint(villainHand, foldNode.board)) continue;

        float villainReachProb = allVillainReachProbs[getReachProbsIndex(hand, nodeIndex, constants, tree)];

        villainTotalReachProb += villainReachProb;

        for (int i = 0; i < tree.gameHandSize; ++i) {
            villainReachProbWithCard[popLowestCardFromSet(villainHand)] += villainReachProb;
        }
        assert(villainHand == 0);
    }

    int heroReward = (foldNode.remainingPlayer == constants.hero) ? foldNode.remainingPlayerReward : -foldNode.remainingPlayerReward;

    for (int hand = 0; hand < heroRangeSize; ++hand) {
        CardSet heroHand = tree.rangeHands[constants.hero][hand];
        if (!areSetsDisjoint(heroHand, foldNode.board)) continue;

        float villainValidReachProb = getValidVillainReachProb(
            hand,
            villainTotalReachProb,
            nodeIndex,
            villainReachProbWithCard,
            constants,
            allVillainReachProbs,
            tree
        );

        allExpectedValues[getExpectedValueIndex(hand, nodeIndex, constants, tree)] += static_cast<float>(heroReward) * villainValidReachProb;
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
    Player hero = constants.hero;
    Player villain = getOpposingPlayer(hero);

    PlayerArray<std::span<const HandData>> sortedHandRanks;
    for (Player player : { hero, villain }) {
        sortedHandRanks[player] = rules.getSortedHandRanks(player, showdownNode.board);
    }

    const auto& heroHands = tree.rangeHands[hero];
    const auto& villainHands = tree.rangeHands[villain];

    int heroRangeSize = tree.rangeSize[hero];
    int villainRangeSize = tree.rangeSize[villain];

    // First pass: Calculate hero winning hands
    {
        float villainTotalReachProb = 0.0f;
        std::array<float, StandardDeckSize> villainReachProbWithCard = {};

        int villainIndexSorted = 0;

        for (HandData heroHandData : sortedHandRanks[hero]) {
            CardSet heroHand = heroHands[heroHandData.index];
            if (!areSetsDisjoint(heroHand, showdownNode.board)) continue;

            while (villainIndexSorted < villainRangeSize && sortedHandRanks[villain][villainIndexSorted].rank < heroHandData.rank) {
                int villainHandIndex = sortedHandRanks[villain][villainIndexSorted].index;
                CardSet villainHand = villainHands[villainHandIndex];

                if (areSetsDisjoint(villainHand, showdownNode.board)) {
                    float villainReachProb = allVillainReachProbs[getReachProbsIndex(villainHandIndex, nodeIndex, constants, tree)];

                    villainTotalReachProb += villainReachProb;

                    for (int i = 0; i < tree.gameHandSize; ++i) {
                        villainReachProbWithCard[popLowestCardFromSet(villainHand)] += villainReachProb;
                    }
                    assert(villainHand == 0);
                }

                ++villainIndexSorted;
            }

            float villainValidReachProb = getValidVillainReachProb(
                heroHandData.index,
                villainTotalReachProb,
                nodeIndex,
                villainReachProbWithCard,
                constants,
                allVillainReachProbs,
                tree
            );

            // Winning hands - positive reward
            allExpectedValues[getExpectedValueIndex(heroHandData.index, nodeIndex, constants, tree)] += static_cast<float>(showdownNode.reward) * villainValidReachProb;
        }
    }

    // Second pass: Calculate hero losing hands
    {
        float villainTotalReachProb = 0.0f;
        std::array<float, StandardDeckSize> villainReachProbWithCard = {};

        int villainIndexSorted = villainRangeSize - 1;

        for (HandData heroHandData : std::views::reverse(sortedHandRanks[hero])) {
            CardSet heroHand = heroHands[heroHandData.index];
            if (!areSetsDisjoint(heroHand, showdownNode.board)) continue;

            while (villainIndexSorted >= 0 && sortedHandRanks[villain][villainIndexSorted].rank > heroHandData.rank) {
                int villainHandIndex = sortedHandRanks[villain][villainIndexSorted].index;
                CardSet villainHand = villainHands[villainHandIndex];

                if (areSetsDisjoint(villainHand, showdownNode.board)) {
                    float villainReachProb = allVillainReachProbs[getReachProbsIndex(villainHandIndex, nodeIndex, constants, tree)];

                    villainTotalReachProb += villainReachProb;

                    for (int i = 0; i < tree.gameHandSize; ++i) {
                        villainReachProbWithCard[popLowestCardFromSet(villainHand)] += villainReachProb;
                    }
                    assert(villainHand == 0);
                }

                --villainIndexSorted;
            }

            float villainValidReachProb = getValidVillainReachProb(
                heroHandData.index,
                villainTotalReachProb,
                nodeIndex,
                villainReachProbWithCard,
                constants,
                allVillainReachProbs,
                tree
            );

            // Losing hands - negative reward
            allExpectedValues[getExpectedValueIndex(heroHandData.index, nodeIndex, constants, tree)] -= static_cast<float>(showdownNode.reward) * villainValidReachProb;
        }
    }

    // Can ignore ties in rakeless game, 0 EV for both players
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
            traverseFold(node.foldNode, nodeIndex, constants, allVillainReachProbs, allExpectedValues, tree);
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
        std::size_t reachIndex = getReachProbsIndex(hand, rootNodeIndex, constants, tree);
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
        expectedValue += allExpectedValues[getExpectedValueIndex(hand, rootNodeIndex, constants, tree)] * heroRangeWeights[hand];
    }

    expectedValue /= tree.totalRangeWeight;

    return expectedValue;
}
} // namespace

DiscountParams getDiscountParams(float alpha, float beta, float gamma, int iteration) {
    float t = static_cast<float>(iteration);
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

void writeAverageStrategyToBuffer(const DecisionNode& decisionNode, Tree& tree) {
    int playerToActRangeSize = tree.rangeSize[decisionNode.player];
    int numActions = decisionNode.decisionDataSize;
    assert(numActions > 0);

    for (int hand = 0; hand < playerToActRangeSize; ++hand) {
        float total = 0.0f;
        for (int action = 0; action < numActions; ++action) {
            float strategySum = tree.allStrategySums[getTrainingDataIndex(action, hand, decisionNode, tree)];
            total += strategySum;
        }

        if (total == 0.0f) {
            // Uniform strategy
            for (int action = 0; action < numActions; ++action) {
                tree.allStrategies[getTrainingDataIndex(action, hand, decisionNode, tree)] = 1.0f / numActions;
            }
        }
        else {
            for (int action = 0; action < numActions; ++action) {
                std::size_t trainingIndex = getTrainingDataIndex(action, hand, decisionNode, tree);
                float strategySum = tree.allStrategySums[trainingIndex];
                tree.allStrategies[trainingIndex] = strategySum / total;
            }
        }
    }
}

std::size_t getTrainingDataIndex(int action, int hand, const DecisionNode& decisionNode, const Tree& tree) {
    assert(action >= 0 && action < decisionNode.decisionDataSize);

    int playerToActRangeSize = tree.rangeSize[decisionNode.player];
    assert(hand >= 0 && hand < playerToActRangeSize);

    return decisionNode.trainingDataOffset + (action * playerToActRangeSize) + hand;
}