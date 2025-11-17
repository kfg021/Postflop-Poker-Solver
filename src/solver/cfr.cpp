#include "solver/cfr.hpp"

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "solver/node.hpp"
#include "solver/tree.hpp"
#include "util/fixed_vector.hpp"
#include "util/stack_allocator.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

// TODO: Fix rounding errors causing isomorphic and non-isomorphic solutions to differ

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

int getThreadIndex() {
    #ifdef _OPENMP
    return omp_get_thread_num();
    #else
    return 0;
    #endif
}

std::size_t getStrategyIndex(int action, int hand, const DecisionNode& decisionNode, const Tree& tree) {
    int playerToActRangeSize = tree.rangeSize[decisionNode.playerToAct];
    assert(hand < playerToActRangeSize);

    int numActions = decisionNode.decisionDataSize;
    assert(action < numActions);

    return action * playerToActRangeSize + hand;
}

FixedVector<float, MaxNumActions> getCurrentStrategy(int hand, const DecisionNode& decisionNode, const Tree& tree) {
    int playerToActRangeSize = tree.rangeSize[decisionNode.playerToAct];
    int numActions = decisionNode.decisionDataSize;
    assert(numActions > 0);

    double totalPositiveRegret = 0.0;

    for (int action = 0; action < numActions; ++action) {
        double regretSum = static_cast<double>(tree.allRegretSums[getTrainingDataIndex(action, hand, decisionNode, tree)]);
        if (regretSum > 0.0) {
            totalPositiveRegret += regretSum;
        }
    }

    if (totalPositiveRegret == 0.0) {
        // Uniform strategy
        return FixedVector<float, MaxNumActions>(numActions, 1.0f / numActions);
    }
    else {
        FixedVector<float, MaxNumActions> currentStrategy(numActions, 0.0f);

        for (int action = 0; action < numActions; ++action) {
            std::size_t trainingIndex = getTrainingDataIndex(action, hand, decisionNode, tree);
            double regretSum = static_cast<double>(tree.allRegretSums[trainingIndex]);
            if (regretSum > 0.0) {
                currentStrategy[action] = regretSum / totalPositiveRegret;
            }
        }

        return currentStrategy;
    }
}

double getValidVillainReachProb(
    int heroHandIndex,
    double villainTotalReachProb,
    const std::array<double, StandardDeckSize>& villainReachProbWithCard,
    const TraversalConstants& constants,
    const StackVector<float>& villainReachProbs,
    const Tree& tree
) {
    CardSet heroHand = tree.rangeHands[constants.hero][heroHandIndex];
    double villainValidReachProb = villainTotalReachProb;

    switch (tree.gameHandSize) {
        case 1:
            villainValidReachProb -= villainReachProbWithCard[getLowestCardInSet(heroHand)];
            break;
        case 2: {
            villainValidReachProb -= villainReachProbWithCard[popLowestCardFromSet(heroHand)];
            assert(getSetSize(heroHand) == 1);
            villainValidReachProb -= villainReachProbWithCard[getLowestCardInSet(heroHand)];

            // Inclusion-Exclusion: Add back the portion of the villain's range that was double subtracted by the above
            int sameHandIndex = tree.sameHandIndexTable[constants.hero][heroHandIndex];
            if (sameHandIndex != -1) {
                villainValidReachProb += villainReachProbs[sameHandIndex];
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
    const TraversalConstants& constants,
    const IGameRules& rules,
    const StackVector<float>& villainReachProbs,
    StackVector<float>& outputExpectedValues,
    Tree& tree,
    StackAllocator<float>& allocator
);

void traverseChance(
    const ChanceNode& chanceNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const StackVector<float>& villainReachProbs,
    StackVector<float>& outputExpectedValues,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    auto areHandAndCardDisjoint = [&tree](Player player, int hand, CardID card) -> bool {
        assert(hand < tree.rangeSize[player]);
        return !setContainsCard(tree.rangeHands[player][hand], card);
    };

    std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

    Player hero = constants.hero;
    Player villain = getOpposingPlayer(hero);

    int heroRangeSize = tree.rangeSize[hero];
    int villainRangeSize = tree.rangeSize[villain];

    // Normalize expected values by the number of total chance cards possible
    // Hero and villain both have a hand
    int chanceCardReachFactor = getSetSize(chanceNode.availableCards) - (2 * tree.gameHandSize);

    for (int cardIndex = 0; cardIndex < chanceNode.chanceDataSize; ++cardIndex) {
        CardID chanceCard = tree.allChanceCards[chanceNode.chanceDataOffset + cardIndex];
        std::size_t nextNodeIndex = tree.allChanceNextNodeIndices[chanceNode.chanceDataOffset + cardIndex];
        assert(nextNodeIndex < tree.allNodes.size());
        const Node& nextNode = tree.allNodes[nextNodeIndex];

        // Update reach probabilities
        StackVector<float> newVillainReachProbs(allocator, getThreadIndex(), villainRangeSize);
        for (int hand = 0; hand < villainRangeSize; ++hand) {
            if (areHandAndCardDisjoint(villain, hand, chanceCard)) {
                newVillainReachProbs[hand] = villainReachProbs[hand] / static_cast<float>(chanceCardReachFactor);
            }
            else {
                newVillainReachProbs[hand] = 0.0f;
            }
        }

        StackVector<float> newOutputExpectedValues(allocator, getThreadIndex(), heroRangeSize);

        traverseTree(nextNode, constants, rules, newVillainReachProbs, newOutputExpectedValues, tree, allocator);

        for (int hand = 0; hand < heroRangeSize; ++hand) {
            if (areHandAndCardDisjoint(hero, hand, chanceCard)) {
                outputExpectedValues[hand] += newOutputExpectedValues[hand];
            }
            else {
                assert(newOutputExpectedValues[hand] == 0.0f);
            }

            // Then calculate contribution for all isomorphisms
            for (SuitMapping mapping : chanceNode.suitMappings) {
                assert(mapping.parent != mapping.child);

                if (mapping.parent == getCardSuit(chanceCard)) {
                    CardID isomorphicCard = getCardIDFromValueAndSuit(getCardValue(chanceCard), mapping.child);
                    int indexAfterSuitSwap = rules.getHandIndexAfterSuitSwap(constants.hero, hand, mapping.parent, mapping.child);

                    if (areHandAndCardDisjoint(hero, hand, isomorphicCard)) {
                        outputExpectedValues[hand] += newOutputExpectedValues[indexAfterSuitSwap];
                    }
                    else {
                        assert(newOutputExpectedValues[indexAfterSuitSwap] == 0.0f);
                    }
                }
            }
        }
    }
}

void traverseDecision(
    const DecisionNode& decisionNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const StackVector<float>& villainReachProbs,
    StackVector<float>& outputExpectedValues,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    // Since StackVector is not default constructible we need to wrap it in an optional to use in FixedVector
    using OptionalStackVector = std::optional<StackVector<float>>;

    auto initOptionalStackVectors = [&allocator](FixedVector<OptionalStackVector, MaxNumActions>& stackVectors, int rangeSize) -> void {
        int threadIndex = getThreadIndex();
        for (OptionalStackVector& stackVector : stackVectors) {
            assert(!stackVector);
            stackVector.emplace(allocator, threadIndex, rangeSize);
        }
    };

    auto calculateActionEVs = [
        &decisionNode,
        &constants,
        &rules,
        &tree,
        &allocator
    ](std::function<const StackVector<float>& (int)> getVillainReachProbsForAction, std::function<StackVector<float>& (int)> getExpectedValueBufferForAction) -> void {
        auto calculateActionEV = [
            &decisionNode,
            &constants,
            &rules,
            &tree,
            &allocator,
            &getVillainReachProbsForAction,
            &getExpectedValueBufferForAction
        ](int action) -> void {
            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];
            traverseTree(nextNode, constants, rules, getVillainReachProbsForAction(action), getExpectedValueBufferForAction(action), tree, allocator);
        };

        int numActions = decisionNode.decisionDataSize;

        #ifdef _OPENMP
        // Not worth trying to parallelize the river because of overhead
        bool shouldParallelize = (decisionNode.street != Street::River);
        if (shouldParallelize) {
            for (int action = 0; action < numActions; ++action) {
                #pragma omp task
                {
                    calculateActionEV(action);
                }
            }

            #pragma omp taskwait
        }
        else {
            for (int action = 0; action < numActions; ++action) {
                calculateActionEV(action);
            }
        }
        #else
        // Run on single thread if no OpenMP
        for (int action = 0; action < numActions; ++action) {
            calculateActionEV(action);
        }
        #endif
    };

    auto heroToActTraining = [
        &decisionNode,
        &constants,
        &rules,
        &villainReachProbs,
        &outputExpectedValues,
        &tree,
        &allocator,
        &initOptionalStackVectors,
        &calculateActionEVs
    ]() {
        std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        int heroRangeSize = tree.rangeSize[constants.hero];
        int villainRangeSize = tree.rangeSize[getOpposingPlayer(constants.hero)];

        int threadIndex = getThreadIndex();

        // Calculate current strategy
        FixedVector<OptionalStackVector, MaxNumActions> currentStrategy(numActions);
        initOptionalStackVectors(currentStrategy, heroRangeSize);

        for (int hand = 0; hand < heroRangeSize; ++hand) {
            FixedVector<float, MaxNumActions> currentStrategyForHand = getCurrentStrategy(hand, decisionNode, tree);
            assert(currentStrategyForHand.size() == numActions);

            for (int action = 0; action < numActions; ++action) {
                assert(currentStrategy[action]);
                (*currentStrategy[action])[hand] = currentStrategyForHand[action];
            }
        }

        // Regret and strategy discounting for DCFR
        if (constants.mode == TraversalMode::DiscountedCfr) {
            for (int action = 0; action < numActions; ++action) {
                for (int hand = 0; hand < heroRangeSize; ++hand) {
                    std::size_t trainingIndex = getTrainingDataIndex(action, hand, decisionNode, tree);
                    float& regretSum = tree.allRegretSums[trainingIndex];
                    float& strategySum = tree.allStrategySums[trainingIndex];

                    if (regretSum > 0.0f) {
                        regretSum *= static_cast<float>(constants.params.alphaT);
                    }
                    else {
                        regretSum *= static_cast<float>(constants.params.betaT);
                    }

                    strategySum *= static_cast<float>(constants.params.gammaT);
                }
            }
        }

        auto getVillainReachProbsForAction = [&villainReachProbs](int /*action*/) -> const StackVector<float>& {
            return villainReachProbs;
        };

        FixedVector<OptionalStackVector, MaxNumActions> newOutputExpectedValues(numActions);
        initOptionalStackVectors(newOutputExpectedValues, heroRangeSize);

        auto getExpectedValueBufferForAction = [&newOutputExpectedValues](int action) -> StackVector<float>& {
            assert(newOutputExpectedValues[action]);
            return *newOutputExpectedValues[action];
        };

        calculateActionEVs(getVillainReachProbsForAction, getExpectedValueBufferForAction);

        // Calculate expected value of strategy
        for (int action = 0; action < numActions; ++action) {
            assert(newOutputExpectedValues[action]);
            assert(currentStrategy[action]);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                outputExpectedValues[hand] += (*newOutputExpectedValues[action])[hand] * (*currentStrategy[action])[hand];
            }
        }

        // Regret and strategy updates
        for (int action = 0; action < numActions; ++action) {
            assert(newOutputExpectedValues[action]);
            assert(currentStrategy[action]);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                std::size_t trainingIndex = getTrainingDataIndex(action, hand, decisionNode, tree);

                float& regretSum = tree.allRegretSums[trainingIndex];
                float& strategySum = tree.allStrategySums[trainingIndex];

                float strategyExpectedValue = outputExpectedValues[hand];
                float actionExpectedValue = (*newOutputExpectedValues[action])[hand];
                regretSum += actionExpectedValue - strategyExpectedValue;

                // TODO: Do we need to weight by hero reach probs?
                strategySum += (*currentStrategy[action])[hand];

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
        &constants,
        &rules,
        &villainReachProbs,
        &outputExpectedValues,
        &tree,
        &allocator,
        &initOptionalStackVectors,
        &calculateActionEVs
    ]() {
        std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        int heroRangeSize = tree.rangeSize[constants.hero];
        int villainRangeSize = tree.rangeSize[getOpposingPlayer(constants.hero)];

        int threadIndex = getThreadIndex();

        // Calculate average strategy
        FixedVector<OptionalStackVector, MaxNumActions> averageStrategy(numActions);
        initOptionalStackVectors(averageStrategy, heroRangeSize);

        for (int hand = 0; hand < heroRangeSize; ++hand) {
            FixedVector<float, MaxNumActions> currentStrategyForHand = getAverageStrategy(hand, decisionNode, tree);
            assert(currentStrategyForHand.size() == numActions);

            for (int action = 0; action < numActions; ++action) {
                assert(averageStrategy[action]);
                (*averageStrategy[action])[hand] = currentStrategyForHand[action];
            }
        }

        auto getVillainReachProbsForAction = [&villainReachProbs](int /*action*/) -> const StackVector<float>& {
            return villainReachProbs;
        };

        FixedVector<OptionalStackVector, MaxNumActions> newOutputExpectedValues(numActions);
        initOptionalStackVectors(newOutputExpectedValues, heroRangeSize);

        auto getExpectedValueBufferForAction = [&newOutputExpectedValues](int action) -> StackVector<float>& {
            assert(newOutputExpectedValues[action]);
            return *newOutputExpectedValues[action];
        };

        calculateActionEVs(getVillainReachProbsForAction, getExpectedValueBufferForAction);

        // Calculate expected value of strategy
        for (int action = 0; action < numActions; ++action) {
            assert(newOutputExpectedValues[action]);
            assert(averageStrategy[action]);
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                outputExpectedValues[hand] += (*newOutputExpectedValues[action])[hand] * (*averageStrategy[action])[hand];
            }
        }
    };

    auto heroToActBestResponse = [
        &decisionNode,
        &constants,
        &rules,
        &villainReachProbs,
        &outputExpectedValues,
        &tree,
        &allocator,
        &initOptionalStackVectors,
        &calculateActionEVs
    ]() -> void {
        // Initialize all EVs to lowest possible 
        static constexpr float Infinity = std::numeric_limits<float>::infinity();
        std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), -Infinity);

        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        int heroRangeSize = tree.rangeSize[constants.hero];

        int threadIndex = getThreadIndex();

        auto getVillainReachProbsForAction = [&villainReachProbs](int /*action*/) -> const StackVector<float>& {
            return villainReachProbs;
        };

        FixedVector<OptionalStackVector, MaxNumActions> newOutputExpectedValues(numActions);
        initOptionalStackVectors(newOutputExpectedValues, heroRangeSize);

        auto getExpectedValueBufferForAction = [&newOutputExpectedValues](int action) -> StackVector<float>& {
            assert(newOutputExpectedValues[action]);
            return *newOutputExpectedValues[action];
        };

        calculateActionEVs(getVillainReachProbsForAction, getExpectedValueBufferForAction);

        // To calculate best response, hero plays the maximally exploitative pure strategy
        for (int action = 0; action < numActions; ++action) {
            assert(newOutputExpectedValues[action]);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                // For each action, play the action that leads to highest EV
                outputExpectedValues[hand] = std::max((*newOutputExpectedValues[action])[hand], outputExpectedValues[hand]);
            }
        }

        for (int hand = 0; hand < heroRangeSize; ++hand) {
            assert(outputExpectedValues[hand] != -Infinity);
        }
    };

    auto villainToAct = [
        &decisionNode,
        &constants,
        &rules,
        &villainReachProbs,
        &outputExpectedValues,
        &tree,
        &allocator,
        &initOptionalStackVectors,
        &calculateActionEVs
    ]() -> void {
        std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        Player villain = getOpposingPlayer(constants.hero);

        int heroRangeSize = tree.rangeSize[constants.hero];
        int villainRangeSize = tree.rangeSize[villain];

        int threadIndex = getThreadIndex();

        // Calculate strategy
        FixedVector<OptionalStackVector, MaxNumActions> strategy(numActions);
        initOptionalStackVectors(strategy, villainRangeSize);

        for (int hand = 0; hand < villainRangeSize; ++hand) {
            FixedVector<float, MaxNumActions> strategyForHand;
            switch (constants.mode) {
                case TraversalMode::VanillaCfr:
                case TraversalMode::CfrPlus:
                case TraversalMode::DiscountedCfr:
                    strategyForHand = getCurrentStrategy(hand, decisionNode, tree);
                    break;
                case TraversalMode::ExpectedValue:
                case TraversalMode::BestResponse:
                    strategyForHand = getAverageStrategy(hand, decisionNode, tree);
                    break;
                default:
                    assert(false);
                    break;
            }
            assert(strategyForHand.size() == numActions);

            for (int action = 0; action < numActions; ++action) {
                assert(strategy[action]);
                (*strategy[action])[hand] = strategyForHand[action];
            }
        }

        FixedVector<OptionalStackVector, MaxNumActions> newVillainReachProbs(numActions);
        initOptionalStackVectors(newVillainReachProbs, villainRangeSize);

        for (int action = 0; action < numActions; ++action) {
            assert(newVillainReachProbs[action]);
            assert(strategy[action]);

            for (int hand = 0; hand < villainRangeSize; ++hand) {
                (*newVillainReachProbs[action])[hand] = villainReachProbs[hand] * (*strategy[action])[hand];
            }
        }
        auto getVillainReachProbsForAction = [&newVillainReachProbs](int action) -> const StackVector<float>& {
            assert(newVillainReachProbs[action]);
            return *newVillainReachProbs[action];
        };

        FixedVector<OptionalStackVector, MaxNumActions> newOutputExpectedValues(numActions);
        initOptionalStackVectors(newOutputExpectedValues, heroRangeSize);

        auto getExpectedValueBufferForAction = [&newOutputExpectedValues](int action) -> StackVector<float>& {
            assert(newOutputExpectedValues[action]);
            return *newOutputExpectedValues[action];
        };

        calculateActionEVs(getVillainReachProbsForAction, getExpectedValueBufferForAction);

        /// Calculate expected value of strategy
        // Not the hero's turn; no strategy or regret updates
        for (int action = 0; action < numActions; ++action) {
            assert(newOutputExpectedValues[action]);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                outputExpectedValues[hand] += (*newOutputExpectedValues[action])[hand];
            }
        }
    };

    if (constants.hero == decisionNode.playerToAct) {
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
    const TraversalConstants& constants,
    const StackVector<float>& villainReachProbs,
    StackVector<float>& outputExpectedValues,
    Tree& tree
) {
    std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

    Player villain = getOpposingPlayer(constants.hero);

    int heroRangeSize = tree.rangeSize[constants.hero];
    int villainRangeSize = tree.rangeSize[villain];

    double villainTotalReachProb = 0.0;
    std::array<double, StandardDeckSize> villainReachProbWithCard = {};

    for (int hand = 0; hand < villainRangeSize; ++hand) {
        CardSet villainHand = tree.rangeHands[villain][hand];
        if (!areSetsDisjoint(villainHand, foldNode.board)) continue;

        double villainReachProb = static_cast<double>(villainReachProbs[hand]);

        villainTotalReachProb += villainReachProb;

        for (int i = 0; i < tree.gameHandSize; ++i) {
            villainReachProbWithCard[popLowestCardFromSet(villainHand)] += villainReachProb;
        }
        assert(villainHand == 0);
    }

    // Winner wins the folding player's wager and the dead money
    // Loser loses their wager
    double winPayoff = static_cast<double>(foldNode.foldingPlayerWager + tree.deadMoney);
    double losePayoff = static_cast<double>(-foldNode.foldingPlayerWager);

    double heroPayoff = (foldNode.foldingPlayer == villain) ? winPayoff : losePayoff;

    for (int hand = 0; hand < heroRangeSize; ++hand) {
        CardSet heroHand = tree.rangeHands[constants.hero][hand];
        if (!areSetsDisjoint(heroHand, foldNode.board)) continue;

        double villainValidReachProb = getValidVillainReachProb(
            hand,
            villainTotalReachProb,
            villainReachProbWithCard,
            constants,
            villainReachProbs,
            tree
        );

        outputExpectedValues[hand] += static_cast<float>(heroPayoff * villainValidReachProb);
    }
}

void traverseShowdown(
    const ShowdownNode& showdownNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const StackVector<float>& villainReachProbs,
    StackVector<float>& outputExpectedValues,
    Tree& tree
) {
    auto addReachProbsToArray = [&tree](std::array<double, StandardDeckSize>& villainReachProbWithCard, CardSet villainHand, double villainReachProb) -> void {
        switch (tree.gameHandSize) {
            case 1:
                villainReachProbWithCard[getLowestCardInSet(villainHand)] += villainReachProb;
                break;
            case 2:
                villainReachProbWithCard[popLowestCardFromSet(villainHand)] += villainReachProb;
                assert(getSetSize(villainHand) == 1);
                villainReachProbWithCard[getLowestCardInSet(villainHand)] += villainReachProb;
                break;
            case 3:
                assert(false);
                break;
        }
    };

    std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

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

    // Winner wins the other player's wager and the dead money
    // Loser loses their wager
    // If the players tie, they split the dead money
    double winPayoff = static_cast<double>(showdownNode.playerWagers + tree.deadMoney);
    double losePayoff = static_cast<double>(-showdownNode.playerWagers);
    double tiePayoff = static_cast<double>(tree.deadMoney) / 2.0;

    // First pass: Calculate hero winning hands
    {
        double villainTotalReachProb = 0.0;
        std::array<double, StandardDeckSize> villainReachProbWithCard = {};

        int villainIndexSorted = 0;

        for (HandData heroHandData : sortedHandRanks[hero]) {
            CardSet heroHand = heroHands[heroHandData.index];
            if (!areSetsDisjoint(heroHand, showdownNode.board)) continue;

            while (villainIndexSorted < villainRangeSize && sortedHandRanks[villain][villainIndexSorted].rank < heroHandData.rank) {
                int villainHandIndex = sortedHandRanks[villain][villainIndexSorted].index;
                CardSet villainHand = villainHands[villainHandIndex];

                if (areSetsDisjoint(villainHand, showdownNode.board)) {
                    double villainReachProb = static_cast<double>(villainReachProbs[villainHandIndex]);
                    villainTotalReachProb += villainReachProb;
                    addReachProbsToArray(villainReachProbWithCard, villainHand, villainReachProb);
                }

                ++villainIndexSorted;
            }

            double villainValidReachProb = getValidVillainReachProb(
                heroHandData.index,
                villainTotalReachProb,
                villainReachProbWithCard,
                constants,
                villainReachProbs,
                tree
            );

            outputExpectedValues[heroHandData.index] += static_cast<float>(winPayoff * villainValidReachProb);
        }
    }

    // Second pass: Calculate hero losing hands
    {
        double villainTotalReachProb = 0.0;
        std::array<double, StandardDeckSize> villainReachProbWithCard = {};

        int villainIndexSorted = villainRangeSize - 1;

        for (HandData heroHandData : std::views::reverse(sortedHandRanks[hero])) {
            CardSet heroHand = heroHands[heroHandData.index];
            if (!areSetsDisjoint(heroHand, showdownNode.board)) continue;

            while (villainIndexSorted >= 0 && sortedHandRanks[villain][villainIndexSorted].rank > heroHandData.rank) {
                int villainHandIndex = sortedHandRanks[villain][villainIndexSorted].index;
                CardSet villainHand = villainHands[villainHandIndex];

                if (areSetsDisjoint(villainHand, showdownNode.board)) {
                    double villainReachProb = static_cast<double>(villainReachProbs[villainHandIndex]);
                    villainTotalReachProb += villainReachProb;
                    addReachProbsToArray(villainReachProbWithCard, villainHand, villainReachProb);
                }

                --villainIndexSorted;
            }

            double villainValidReachProb = getValidVillainReachProb(
                heroHandData.index,
                villainTotalReachProb,
                villainReachProbWithCard,
                constants,
                villainReachProbs,
                tree
            );

            outputExpectedValues[heroHandData.index] += static_cast<float>(losePayoff * villainValidReachProb);
        }
    }

    // Third pass: Calculate tie hands
    // Can ignore ties in zero-sum game, 0 EV for both players
    if (tree.deadMoney > 0) {
        double villainTotalReachProb = 0.0;
        std::array<double, StandardDeckSize> villainReachProbWithCard = {};

        int villainIndexSorted = 0;

        for (int heroIndexSorted = 0; heroIndexSorted < heroRangeSize; ++heroIndexSorted) {
            HandData heroHandData = sortedHandRanks[hero][heroIndexSorted];
            CardSet heroHand = heroHands[heroHandData.index];
            if (!areSetsDisjoint(heroHand, showdownNode.board)) continue;

            bool heroRankIncreased = (heroIndexSorted == 0) || (heroHandData.rank > sortedHandRanks[hero][heroIndexSorted - 1].rank);
            if (heroRankIncreased) {
                // We need to reset our reach probs because the hero's rank has increased
                villainTotalReachProb = 0.0;
                villainReachProbWithCard.fill(0.0);

                // Skip until we find a hand that we tie with
                while (villainIndexSorted < villainRangeSize && sortedHandRanks[villain][villainIndexSorted].rank < heroHandData.rank) {
                    ++villainIndexSorted;
                }

                while (villainIndexSorted < villainRangeSize && sortedHandRanks[villain][villainIndexSorted].rank == heroHandData.rank) {
                    int villainHandIndex = sortedHandRanks[villain][villainIndexSorted].index;
                    CardSet villainHand = villainHands[villainHandIndex];

                    if (areSetsDisjoint(villainHand, showdownNode.board)) {
                        double villainReachProb = static_cast<double>(villainReachProbs[villainHandIndex]);
                        villainTotalReachProb += villainReachProb;
                        addReachProbsToArray(villainReachProbWithCard, villainHand, villainReachProb);
                    }

                    ++villainIndexSorted;
                }
            }

            double villainValidReachProb = getValidVillainReachProb(
                heroHandData.index,
                villainTotalReachProb,
                villainReachProbWithCard,
                constants,
                villainReachProbs,
                tree
            );

            outputExpectedValues[heroHandData.index] += static_cast<float>(tiePayoff * villainValidReachProb);
        }
    }
}

void traverseTree(
    const Node& node,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const StackVector<float>& villainReachProbs,
    StackVector<float>& outputExpectedValues,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    assert(tree.isTreeSkeletonBuilt() && tree.isFullTreeBuilt());

    switch (node.getNodeType()) {
        case NodeType::Chance:
            traverseChance(node.chanceNode, constants, rules, villainReachProbs, outputExpectedValues, tree, allocator);
            break;
        case NodeType::Decision:
            traverseDecision(node.decisionNode, constants, rules, villainReachProbs, outputExpectedValues, tree, allocator);
            break;
        case NodeType::Fold:
            traverseFold(node.foldNode, constants, villainReachProbs, outputExpectedValues, tree);
            break;
        case NodeType::Showdown:
            traverseShowdown(node.showdownNode, constants, rules, villainReachProbs, outputExpectedValues, tree);
            break;
        default:
            assert(false);
            break;
    }
}

void traverseFromRoot(const TraversalConstants& constants, const IGameRules& rules, StackVector<float>& outputExpectedValues, Tree& tree, StackAllocator<float>& allocator) {
    Player villain = getOpposingPlayer(constants.hero);
    const auto& initialRangeWeights = rules.getInitialRangeWeights(villain);

    int heroRangeSize = tree.rangeSize[constants.hero];
    int villainRangeSize = tree.rangeSize[villain];

    StackVector<float> villainReachProbs(allocator, getThreadIndex(), villainRangeSize);
    for (int hand = 0; hand < villainRangeSize; ++hand) {
        villainReachProbs[hand] = initialRangeWeights[hand];
    }

    return traverseTree(
        tree.allNodes[tree.getRootNodeIndex()],
        constants,
        rules,
        villainReachProbs,
        outputExpectedValues,
        tree,
        allocator
    );
}

float rootExpectedValue(
    Player hero,
    const IGameRules& rules,
    Tree& tree,
    TraversalMode mode,
    StackAllocator<float>& allocator
) {
    assert((mode == TraversalMode::ExpectedValue) || (mode == TraversalMode::BestResponse));

    // Allocator should be empty before starting traversal, otherwise something wasn't deleted correctly
    assert(allocator.isEmpty());

    TraversalConstants constants = {
       .hero = hero,
       .mode = mode,
       .params = {} // No params needed for expected value
    };

    int heroRangeSize = tree.rangeSize[hero];
    StackVector<float> outputExpectedValues(allocator, getThreadIndex(), heroRangeSize);

    traverseFromRoot(constants, rules, outputExpectedValues, tree, allocator);

    const auto& heroRangeWeights = rules.getInitialRangeWeights(hero);
    double expectedValue = 0.0;
    for (int hand = 0; hand < heroRangeSize; ++hand) {
        expectedValue += static_cast<double>(outputExpectedValues[hand]) * static_cast<double>(heroRangeWeights[hand]);
    }
    expectedValue /= tree.totalRangeWeight;
    return static_cast<float>(expectedValue);
}
} // namespace

DiscountParams getDiscountParams(float alpha, float beta, float gamma, int iteration) {
    double t = static_cast<float>(iteration);
    double a = std::pow(t, static_cast<double>(alpha));
    double b = std::pow(t, static_cast<double>(beta));

    return {
        .alphaT = a / (a + 1),
        .betaT = b / (b + 1),
        .gammaT = std::pow(t / (t + 1), static_cast<double>(gamma))
    };
}

void vanillaCfr(
    Player hero,
    const IGameRules& rules,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    // Allocator should be empty before starting traversal, otherwise something wasn't deleted correctly
    assert(allocator.isEmpty());

    TraversalConstants constants = {
        .hero = hero,
        .mode = TraversalMode::VanillaCfr,
        .params = {} // No params needed for vanilla CFR
    };

    StackVector<float> outputExpectedValues(allocator, getThreadIndex(), tree.rangeSize[hero]);
    traverseFromRoot(constants, rules, outputExpectedValues, tree, allocator);
}

void cfrPlus(
    Player hero,
    const IGameRules& rules,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    // Allocator should be empty before starting traversal, otherwise something wasn't deleted correctly
    assert(allocator.isEmpty());

    TraversalConstants constants = {
        .hero = hero,
        .mode = TraversalMode::CfrPlus,
        .params = {} // No params needed for CFR+
    };

    StackVector<float> outputExpectedValues(allocator, getThreadIndex(), tree.rangeSize[hero]);
    traverseFromRoot(constants, rules, outputExpectedValues, tree, allocator);
}

void discountedCfr(
    Player hero,
    const IGameRules& rules,
    const DiscountParams& params,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    // Allocator should be empty before starting traversal, otherwise something wasn't deleted correctly
    assert(allocator.isEmpty());

    TraversalConstants constants = {
        .hero = hero,
        .mode = TraversalMode::DiscountedCfr,
        .params = params
    };

    StackVector<float> outputExpectedValues(allocator, getThreadIndex(), tree.rangeSize[hero]);
    traverseFromRoot(constants, rules, outputExpectedValues, tree, allocator);
}

float expectedValue(
    Player hero,
    const IGameRules& rules,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    return rootExpectedValue(hero, rules, tree, TraversalMode::ExpectedValue, allocator);
}

float bestResponseEV(
    Player hero,
    const IGameRules& rules,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    return rootExpectedValue(hero, rules, tree, TraversalMode::BestResponse, allocator);
}

float calculateExploitability(const IGameRules& rules, Tree& tree, StackAllocator<float>& allocator) {
    float player0BestResponseEV = bestResponseEV(Player::P0, rules, tree, allocator);
    float player1BestResponseEV = bestResponseEV(Player::P1, rules, tree, allocator);

    float player0ExpectedValue = expectedValue(Player::P0, rules, tree, allocator);
    float player1ExpectedValue = expectedValue(Player::P1, rules, tree, allocator);

    // Exploitative strategies should always be at least as strong as the Nash strategy
    assert(player0BestResponseEV >= player0ExpectedValue);
    assert(player1BestResponseEV >= player1ExpectedValue);

    float player0Distance = player0BestResponseEV - player0ExpectedValue;
    float player1Distance = player1BestResponseEV - player1ExpectedValue;

    float exploitability = (player0Distance + player1Distance) / 2.0f;
    return exploitability;
}

float calculateExploitabilityFast(const IGameRules& rules, Tree& tree, StackAllocator<float>& allocator) {
    // Speeds up the exploitability calculation by assuming that EV(Player0) + EV(Player1) = dead money
    // This is true in theory but not always true from the CFR calculated strategies
    float player0BestResponseEV = bestResponseEV(Player::P0, rules, tree, allocator);
    float player1BestResponseEV = bestResponseEV(Player::P1, rules, tree, allocator);
    float exploitability = (player0BestResponseEV + player1BestResponseEV - tree.deadMoney) / 2.0f;
    return exploitability;
}

FixedVector<float, MaxNumActions> getAverageStrategy(int hand, const DecisionNode& decisionNode, const Tree& tree) {
    int playerToActRangeSize = tree.rangeSize[decisionNode.playerToAct];
    int numActions = decisionNode.decisionDataSize;
    assert(numActions > 0);

    double total = 0.0;
    for (int action = 0; action < numActions; ++action) {
        double strategySum = static_cast<double>(tree.allStrategySums[getTrainingDataIndex(action, hand, decisionNode, tree)]);
        total += strategySum;
    }

    if (total == 0.0) {
        // Uniform strategy
        return FixedVector<float, MaxNumActions>(numActions, 1.0f / numActions);
    }
    else {
        FixedVector<float, MaxNumActions> averageStrategy(numActions, 0.0f);

        for (int action = 0; action < numActions; ++action) {
            std::size_t trainingIndex = getTrainingDataIndex(action, hand, decisionNode, tree);
            averageStrategy[action] = tree.allStrategySums[trainingIndex] / total;
        }

        return averageStrategy;
    }
}

std::size_t getTrainingDataIndex(int action, int hand, const DecisionNode& decisionNode, const Tree& tree) {
    assert(action >= 0 && action < decisionNode.decisionDataSize);

    int playerToActRangeSize = tree.rangeSize[decisionNode.playerToAct];
    assert(hand >= 0 && hand < playerToActRangeSize);

    return decisionNode.trainingDataOffset + (action * playerToActRangeSize) + hand;
}