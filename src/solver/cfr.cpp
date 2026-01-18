#include "solver/cfr.hpp"

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "solver/tree.hpp"
#include "util/fixed_vector.hpp"
#include "util/stack_allocator.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

// TODO: Fix rounding errors causing isomorphic and non-isomorphic solutions to differ
// TODO: Add back intermediate calculation with doubles

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

bool shouldParallelize(Street currentStreet, Street startingStreet) {
    return (currentStreet == startingStreet) && (startingStreet != Street::River);
}

std::size_t getTrainingDataActionOffset(int action, const Node& decisionNode, const Tree& tree) {
    assert(decisionNode.nodeType == NodeType::Decision);
    assert(action >= 0 && action < decisionNode.numChildren);
    return decisionNode.trainingDataOffset + (action * tree.rangeSize[decisionNode.state.playerToAct]);
}

void writeCurrentStrategyToBuffer(std::span<float> currentStrategyBuffer, const Node& decisionNode, const Tree& tree, StackAllocator<float>& allocator) {
    assert(decisionNode.nodeType == NodeType::Decision);

    int numActions = decisionNode.numChildren;
    int playerToActRangeSize = tree.rangeSize[decisionNode.state.playerToAct];
    assert(numActions > 0);
    assert(currentStrategyBuffer.size() == numActions * playerToActRangeSize);

    ScopedVector<float> totalPositiveRegrets(allocator, getThreadIndex(), playerToActRangeSize);
    std::fill(totalPositiveRegrets.begin(), totalPositiveRegrets.end(), 0.0f);

    for (int action = 0; action < numActions; ++action) {
        std::size_t trainingActionOffset = getTrainingDataActionOffset(action, decisionNode, tree);
        for (int hand = 0; hand < playerToActRangeSize; ++hand) {
            totalPositiveRegrets[hand] += std::max(tree.allRegretSums[trainingActionOffset + hand], 0.0f);
        }
    }

    for (int action = 0; action < numActions; ++action) {
        std::size_t trainingActionOffset = getTrainingDataActionOffset(action, decisionNode, tree);
        for (int hand = 0; hand < playerToActRangeSize; ++hand) {
            if (totalPositiveRegrets[hand] > 0.0f) {
                currentStrategyBuffer[action * playerToActRangeSize + hand] = std::max(tree.allRegretSums[trainingActionOffset + hand], 0.0f) / totalPositiveRegrets[hand];
            }
            else {
                // Play a uniform strategy if no action has positive regret
                assert(totalPositiveRegrets[hand] == 0.0f);
                currentStrategyBuffer[action * playerToActRangeSize + hand] = 1.0f / static_cast<float>(numActions);
            }
        }
    }
}

void writeAverageStrategyToBuffer(std::span<float> averageStrategyBuffer, const Node& decisionNode, const Tree& tree, StackAllocator<float>& allocator) {
    assert(decisionNode.nodeType == NodeType::Decision);

    int numActions = decisionNode.numChildren;
    int playerToActRangeSize = tree.rangeSize[decisionNode.state.playerToAct];
    assert(numActions > 0);
    assert(averageStrategyBuffer.size() == numActions * playerToActRangeSize);

    ScopedVector<float> totalStrategy(allocator, getThreadIndex(), playerToActRangeSize);
    std::fill(totalStrategy.begin(), totalStrategy.end(), 0.0f);

    for (int action = 0; action < numActions; ++action) {
        std::size_t trainingActionOffset = getTrainingDataActionOffset(action, decisionNode, tree);
        for (int hand = 0; hand < playerToActRangeSize; ++hand) {
            assert(tree.allStrategySums[trainingActionOffset + hand] >= 0.0f);
            totalStrategy[hand] += tree.allStrategySums[trainingActionOffset + hand];
        }
    }

    for (int action = 0; action < numActions; ++action) {
        std::size_t trainingActionOffset = getTrainingDataActionOffset(action, decisionNode, tree);
        for (int hand = 0; hand < playerToActRangeSize; ++hand) {
            if (totalStrategy[hand] > 0.0f) {
                averageStrategyBuffer[action * playerToActRangeSize + hand] = tree.allStrategySums[trainingActionOffset + hand] / totalStrategy[hand];
            }
            else {
                // Play a uniform strategy if we don't have a strategy yet
                assert(totalStrategy[hand] == 0.0f);
                averageStrategyBuffer[action * playerToActRangeSize + hand] = 1.0f / static_cast<float>(numActions);
            }
        }
    }
}

bool areHandAndCardDisjoint(Player player, int hand, CardID card, const Tree& tree) {
    assert(hand < tree.rangeSize[player]);
    const auto& rangeHandCards = tree.rangeHandCards[player];
    switch (tree.gameHandSize) {
        case 1:
            return rangeHandCards[hand] != card;
        case 2:
            return (rangeHandCards[2 * hand] != card) && (rangeHandCards[2 * hand + 1] != card);
        default:
            assert(false);
            return false;
    }
}

bool areHandAndSetDisjoint(Player player, int hand, CardSet cardSet, const Tree& tree) {
    assert(hand < tree.rangeSize[player]);
    const auto& rangeHandCards = tree.rangeHandCards[player];
    switch (tree.gameHandSize) {
        case 1:
            return !setContainsCard(cardSet, rangeHandCards[hand]);
        case 2:
            return !setContainsCard(cardSet, rangeHandCards[2 * hand]) && !setContainsCard(cardSet, rangeHandCards[2 * hand + 1]);
        default:
            assert(false);
            return false;
    }
}

void addReachProbsToArray(std::array<float, StandardDeckSize>& villainReachProbWithCard, int villainHandIndex, float villainReachProb, const TraversalConstants& constants, const Tree& tree) {
    Player villain = getOpposingPlayer(constants.hero);
    assert(villainHandIndex < tree.rangeSize[villain]);
    const auto& rangeHandCards = tree.rangeHandCards[villain];
    switch (tree.gameHandSize) {
        case 1:
            villainReachProbWithCard[rangeHandCards[villainHandIndex]] += villainReachProb;
            break;
        case 2:
            villainReachProbWithCard[rangeHandCards[2 * villainHandIndex]] += villainReachProb;
            villainReachProbWithCard[rangeHandCards[2 * villainHandIndex + 1]] += villainReachProb;
            break;
        default:
            assert(false);
            break;
    }
}

float getReachProbBlockedByHeroHand(
    int heroHandIndex,
    const std::array<float, StandardDeckSize>& villainReachProbWithCard,
    const TraversalConstants& constants,
    std::span<const float> villainReachProbs,
    const Tree& tree
) {
    const auto& rangeHandCards = tree.rangeHandCards[constants.hero];
    switch (tree.gameHandSize) {
        case 1:
            return villainReachProbWithCard[rangeHandCards[heroHandIndex]];
        case 2:
            return villainReachProbWithCard[rangeHandCards[2 * heroHandIndex]] + villainReachProbWithCard[rangeHandCards[2 * heroHandIndex + 1]];
        default:
            assert(false);
            return 0.0f;
    }
}

// Returns the portion of the villain's range that was double-counted in the above function
float getInclusionExculsionCorrection(
    int heroHandIndex,
    const TraversalConstants& constants,
    std::span<const float> villainReachProbs,
    const Tree& tree
) {
    switch (tree.gameHandSize) {
        case 1:
            return 0.0f;
        case 2: {
            std::int16_t sameHandIndex = tree.sameHandIndexTable[constants.hero][heroHandIndex];
            return (sameHandIndex != -1) ? villainReachProbs[sameHandIndex] : 0.0f;
        }
        default:
            // For hand size > 2, logic is more complicated
            assert(false);
            return 0.0f;
    }
}

void traverseTree(
    const Node& node,
    const TraversalConstants& constants,
    const IGameRules& rules,
    std::span<const float> villainReachProbs,
    std::span<float> outputExpectedValues,
    Tree& tree,
    StackAllocator<float>& allocator
);

void traverseChance(
    const Node& chanceNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    std::span<const float> villainReachProbs,
    std::span<float> outputExpectedValues,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    auto calculateCardEV = [
        &chanceNode,
        &constants,
        &rules,
        &tree,
        &villainReachProbs,
        &allocator
    ](int cardIndex, std::span<float> newOutputExpectedValues) -> void {
        Player villain = getOpposingPlayer(constants.hero);

        int heroRangeSize = tree.rangeSize[constants.hero];
        int villainRangeSize = tree.rangeSize[villain];

        const Node& nextNode = tree.allNodes[chanceNode.childrenOffset + cardIndex];
        CardID chanceCard = nextNode.state.lastDealtCard;

        // Normalize expected values by the number of total chance cards possible
        // Hero and villain both have a hand
        int chanceCardReachFactor = getSetSize(chanceNode.availableCards) - (2 * tree.gameHandSize);

        ScopedVector<float> newVillainReachProbs(allocator, getThreadIndex(), villainRangeSize);
        for (int hand = 0; hand < villainRangeSize; ++hand) {
            if (areHandAndCardDisjoint(villain, hand, chanceCard, tree)) {
                newVillainReachProbs[hand] = villainReachProbs[hand] / static_cast<float>(chanceCardReachFactor);
            }
            else {
                newVillainReachProbs[hand] = 0.0f;
            }
        }

        auto evCardRangeBegin = newOutputExpectedValues.begin() + cardIndex * heroRangeSize;
        auto evCardRangeEnd = evCardRangeBegin + heroRangeSize;
        traverseTree(nextNode, constants, rules, newVillainReachProbs.getData(), { evCardRangeBegin, evCardRangeEnd }, tree, allocator);
    };

    assert(chanceNode.nodeType == NodeType::Chance);

    std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

    int heroRangeSize = tree.rangeSize[constants.hero];

    ScopedVector<float> newOutputExpectedValues(allocator, getThreadIndex(), chanceNode.numChildren * heroRangeSize);
    std::span<float> newOutputExpectedValuesData = newOutputExpectedValues.getData();

    #ifdef _OPENMP
    if (shouldParallelize(chanceNode.state.currentStreet, tree.startingStreet)) {
        for (int cardIndex = 0; cardIndex < chanceNode.numChildren; ++cardIndex) {
            #pragma omp task default(none) firstprivate(calculateCardEV, cardIndex, newOutputExpectedValuesData)
            {
                calculateCardEV(cardIndex, newOutputExpectedValuesData);
            }
        }

        #pragma omp taskwait
    }
    else {
        for (int cardIndex = 0; cardIndex < chanceNode.numChildren; ++cardIndex) {
            calculateCardEV(cardIndex, newOutputExpectedValuesData);
        }
    }
    #else
    // Run on single thread if no OpenMP
    for (int cardIndex = 0; cardIndex < chanceNode.numChildren; ++cardIndex) {
        calculateCardEV(cardIndex, newOutputExpectedValuesData);
    }
    #endif

    for (int cardIndex = 0; cardIndex < chanceNode.numChildren; ++cardIndex) {
        CardID chanceCard = tree.allNodes[chanceNode.childrenOffset + cardIndex].state.lastDealtCard;

        for (int hand = 0; hand < heroRangeSize; ++hand) {
            if (areHandAndCardDisjoint(constants.hero, hand, chanceCard, tree)) {
                outputExpectedValues[hand] += newOutputExpectedValues[cardIndex * heroRangeSize + hand];
            }
            else {
                assert(newOutputExpectedValues[cardIndex * heroRangeSize + hand] == 0.0f);
            }

            // Then calculate contribution for all isomorphisms
            for (SuitMapping mapping : chanceNode.suitMappings) {
                assert(mapping.parent != mapping.child);

                if (mapping.parent == getCardSuit(chanceCard)) {
                    CardID isomorphicCard = getCardIDFromValueAndSuit(getCardValue(chanceCard), mapping.child);
                    int indexAfterSuitSwap = rules.getHandIndexAfterSuitSwap(constants.hero, hand, mapping.parent, mapping.child);

                    if (areHandAndCardDisjoint(constants.hero, hand, isomorphicCard, tree)) {
                        outputExpectedValues[hand] += newOutputExpectedValues[cardIndex * heroRangeSize + indexAfterSuitSwap];
                    }
                    else {
                        assert(newOutputExpectedValues[cardIndex * heroRangeSize + indexAfterSuitSwap] == 0.0f);
                    }
                }
            }
        }
    }
}

// TODO: Consider making this a template
void traverseDecision(
    const Node& decisionNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    std::span<const float> villainReachProbs,
    std::span<float> outputExpectedValues,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    // TODO: Strategy is unused in calculateActionEVHero, but we need to provide it for calculateActionEVVillain
    auto calculateActionEVs = [
        &decisionNode,
        &constants,
        &rules,
        &villainReachProbs,
        &tree,
        &allocator
    ](std::span<float> newOutputExpectedValues, std::span<const float> strategy) -> void {
        auto calculateActionEVHero = [
            &decisionNode,
            &constants,
            &rules,
            &villainReachProbs,
            &tree,
            &allocator,
            &newOutputExpectedValues
        ](int action) -> void {
            int heroRangeSize = tree.rangeSize[constants.hero];
            auto evActionRangeBegin = newOutputExpectedValues.begin() + action * heroRangeSize;
            auto evActionRangeEnd = evActionRangeBegin + heroRangeSize;

            // For the hero we copy the villain reach probs from the previous level
            traverseTree(tree.allNodes[decisionNode.childrenOffset + action], constants, rules, villainReachProbs, { evActionRangeBegin, evActionRangeEnd }, tree, allocator);
        };

        auto calculateActionEVVillain = [
            &decisionNode,
            &constants,
            &rules,
            &villainReachProbs,
            &tree,
            &allocator,
            &newOutputExpectedValues,
            &strategy
        ](int action) -> void {
            assert(!strategy.empty());

            Player villain = getOpposingPlayer(constants.hero);

            int heroRangeSize = tree.rangeSize[constants.hero];
            int villainRangeSize = tree.rangeSize[villain];

            // For the villain we need to modify the villain reach probs
            ScopedVector<float> newVillainReachProbs(allocator, getThreadIndex(), villainRangeSize);
            for (int hand = 0; hand < villainRangeSize; ++hand) {
                newVillainReachProbs[hand] = villainReachProbs[hand] * strategy[action * villainRangeSize + hand];
            }

            auto evActionRangeBegin = newOutputExpectedValues.begin() + action * heroRangeSize;
            auto evActionRangeEnd = evActionRangeBegin + heroRangeSize;
            traverseTree(tree.allNodes[decisionNode.childrenOffset + action], constants, rules, newVillainReachProbs.getData(), { evActionRangeBegin, evActionRangeEnd }, tree, allocator);
        };

        auto calculateActionEV = [
            &decisionNode,
            &constants,
            &calculateActionEVHero,
            &calculateActionEVVillain
        ](int action) -> void {
            return (decisionNode.state.playerToAct == constants.hero) ? calculateActionEVHero(action) : calculateActionEVVillain(action);
        };

        assert(decisionNode.nodeType == NodeType::Decision);

        int numActions = decisionNode.numChildren;

        #ifdef _OPENMP
        if (shouldParallelize(decisionNode.state.currentStreet, tree.startingStreet)) {
            for (int action = 0; action < numActions; ++action) {
                #pragma omp task default(none) firstprivate(calculateActionEV, action)
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
        &calculateActionEVs
    ]() {
        std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

        int numActions = static_cast<int>(decisionNode.numChildren);
        assert(numActions > 0);

        int heroRangeSize = tree.rangeSize[constants.hero];
        int villainRangeSize = tree.rangeSize[getOpposingPlayer(constants.hero)];

        // Calculate current strategy
        ScopedVector<float> currentStrategy(allocator, getThreadIndex(), numActions * heroRangeSize);
        writeCurrentStrategyToBuffer(currentStrategy.getData(), decisionNode, tree, allocator);

        // Regret and strategy discounting for DCFR
        if (constants.mode == TraversalMode::DiscountedCfr) {
            float alpha = static_cast<float>(constants.params.alphaT);
            float beta = static_cast<float>(constants.params.betaT);
            float gamma = static_cast<float>(constants.params.gammaT);

            for (int action = 0; action < numActions; ++action) {
                std::size_t trainingActionOffset = getTrainingDataActionOffset(action, decisionNode, tree);
                for (int hand = 0; hand < heroRangeSize; ++hand) {
                    float& regretSum = tree.allRegretSums[trainingActionOffset + hand];
                    float& strategySum = tree.allStrategySums[trainingActionOffset + hand];

                    regretSum *= (regretSum > 0.0f) ? alpha : beta;
                    strategySum *= gamma;
                }
            }
        }

        ScopedVector<float> newOutputExpectedValues(allocator, getThreadIndex(), numActions * heroRangeSize);
        calculateActionEVs(newOutputExpectedValues.getData(), {});

        // Calculate expected value of strategy
        for (int action = 0; action < numActions; ++action) {
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                outputExpectedValues[hand] += newOutputExpectedValues[action * heroRangeSize + hand] * currentStrategy[action * heroRangeSize + hand];
            }
        }

        // Regret and strategy updates
        for (int action = 0; action < numActions; ++action) {
            std::size_t trainingActionOffset = getTrainingDataActionOffset(action, decisionNode, tree);
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                float& regretSum = tree.allRegretSums[trainingActionOffset + hand];
                float& strategySum = tree.allStrategySums[trainingActionOffset + hand];

                float strategyExpectedValue = outputExpectedValues[hand];
                float actionExpectedValue = newOutputExpectedValues[action * heroRangeSize + hand];
                regretSum += actionExpectedValue - strategyExpectedValue;

                // TODO: Do we need to weight by hero reach probs?
                strategySum += currentStrategy[action * heroRangeSize + hand];

                // In CFR+, we erase negative regrets for faster convergence
                if (constants.mode == TraversalMode::CfrPlus) {
                    regretSum = std::max(regretSum, 0.0f);
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
        &calculateActionEVs
    ]() {
        std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

        int numActions = static_cast<int>(decisionNode.numChildren);
        assert(numActions > 0);

        int heroRangeSize = tree.rangeSize[constants.hero];
        int villainRangeSize = tree.rangeSize[getOpposingPlayer(constants.hero)];

        // Calculate average strategy
        ScopedVector<float> averageStrategy(allocator, getThreadIndex(), numActions * heroRangeSize);
        writeAverageStrategyToBuffer(averageStrategy.getData(), decisionNode, tree, allocator);

        ScopedVector<float> newOutputExpectedValues(allocator, getThreadIndex(), numActions * heroRangeSize);
        calculateActionEVs(newOutputExpectedValues.getData(), {});

        // Calculate expected value of strategy
        for (int action = 0; action < numActions; ++action) {
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                outputExpectedValues[hand] += newOutputExpectedValues[action * heroRangeSize + hand] * averageStrategy[action * heroRangeSize + hand];
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
        &calculateActionEVs
    ]() -> void {
        // Initialize all EVs to lowest possible 
        static constexpr float Lowest = std::numeric_limits<float>::lowest();
        std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), Lowest);

        int numActions = static_cast<int>(decisionNode.numChildren);
        assert(numActions > 0);

        int heroRangeSize = tree.rangeSize[constants.hero];

        ScopedVector<float> newOutputExpectedValues(allocator, getThreadIndex(), numActions * heroRangeSize);
        calculateActionEVs(newOutputExpectedValues.getData(), {});

        // To calculate best response, hero plays the maximally exploitative pure strategy
        for (int action = 0; action < numActions; ++action) {
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                // For each action, play the action that leads to highest EV
                outputExpectedValues[hand] = std::max(outputExpectedValues[hand], newOutputExpectedValues[action * heroRangeSize + hand]);
            }
        }

        for (int hand = 0; hand < heroRangeSize; ++hand) {
            assert(outputExpectedValues[hand] != Lowest);
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
        &calculateActionEVs
    ]() -> void {
        std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

        int numActions = static_cast<int>(decisionNode.numChildren);
        assert(numActions > 0);

        Player villain = getOpposingPlayer(constants.hero);

        int heroRangeSize = tree.rangeSize[constants.hero];
        int villainRangeSize = tree.rangeSize[villain];

        // Calculate strategy
        ScopedVector<float> strategy(allocator, getThreadIndex(), numActions * villainRangeSize);
        switch (constants.mode) {
            case TraversalMode::VanillaCfr:
            case TraversalMode::CfrPlus:
            case TraversalMode::DiscountedCfr:
                writeCurrentStrategyToBuffer(strategy.getData(), decisionNode, tree, allocator);
                break;
            case TraversalMode::ExpectedValue:
            case TraversalMode::BestResponse:
                writeAverageStrategyToBuffer(strategy.getData(), decisionNode, tree, allocator);
                break;
            default:
                assert(false);
                break;
        }

        ScopedVector<float> newVillainReachProbs(allocator, getThreadIndex(), numActions * villainRangeSize);
        for (int action = 0; action < numActions; ++action) {
            for (int hand = 0; hand < villainRangeSize; ++hand) {
                newVillainReachProbs[action * villainRangeSize + hand] = villainReachProbs[hand] * strategy[action * villainRangeSize + hand];
            }
        }

        ScopedVector<float> newOutputExpectedValues(allocator, getThreadIndex(), numActions * heroRangeSize);
        calculateActionEVs(newOutputExpectedValues.getData(), strategy.getData());

        /// Calculate expected value of strategy
        // Not the hero's turn; no strategy or regret updates
        for (int action = 0; action < numActions; ++action) {
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                outputExpectedValues[hand] += newOutputExpectedValues[action * heroRangeSize + hand];
            }
        }
    };

    if (constants.hero == decisionNode.state.playerToAct) {
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
    const Node& foldNode,
    const TraversalConstants& constants,
    std::span<const float> villainReachProbs,
    std::span<float> outputExpectedValues,
    Tree& tree
) {
    assert(foldNode.nodeType == NodeType::Fold);

    std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

    Player villain = getOpposingPlayer(constants.hero);

    int heroRangeSize = tree.rangeSize[constants.hero];
    int villainRangeSize = tree.rangeSize[villain];

    float villainTotalReachProb = 0.0f;
    std::array<float, StandardDeckSize> villainReachProbWithCard = {};

    for (int hand = 0; hand < villainRangeSize; ++hand) {
        if (!areHandAndSetDisjoint(villain, hand, foldNode.state.currentBoard, tree)) continue;

        float villainReachProb = villainReachProbs[hand];
        villainTotalReachProb += villainReachProb;
        addReachProbsToArray(villainReachProbWithCard, hand, villainReachProb, constants, tree);
    }

    if (villainTotalReachProb == 0.0f) {
        return;
    }

    // The folding player acted last turn
    Player foldingPlayer = getOpposingPlayer(foldNode.state.playerToAct);
    int foldingPlayerWager = foldNode.state.totalWagers[foldingPlayer];

    // Winner wins the folding player's wager and the dead money
    // Loser loses their wager
    float winPayoff = static_cast<float>(foldingPlayerWager + tree.deadMoney);
    float losePayoff = static_cast<float>(-foldingPlayerWager);

    float heroPayoff = (foldingPlayer == villain) ? winPayoff : losePayoff;

    for (int hand = 0; hand < heroRangeSize; ++hand) {
        if (!areHandAndSetDisjoint(constants.hero, hand, foldNode.state.currentBoard, tree)) continue;

        float villainValidReachProb = villainTotalReachProb
            - getReachProbBlockedByHeroHand(hand, villainReachProbWithCard, constants, villainReachProbs, tree)
            + getInclusionExculsionCorrection(hand, constants, villainReachProbs, tree);

        outputExpectedValues[hand] += heroPayoff * villainValidReachProb;
    }
}

void traverseShowdown(
    const Node& showdownNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    std::span<const float> villainReachProbs,
    std::span<float> outputExpectedValues,
    Tree& tree
) {
    assert(showdownNode.nodeType == NodeType::Showdown);

    std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

    Player hero = constants.hero;
    Player villain = getOpposingPlayer(hero);

    PlayerArray<std::span<const HandData>> sortedHandRanks;
    for (Player player : { hero, villain }) {
        sortedHandRanks[player] = rules.getValidSortedHandRanks(player, showdownNode.state.currentBoard);
    }

    int heroFilteredRangeSize = sortedHandRanks[hero].size();
    int villainFilteredRangeSize = sortedHandRanks[villain].size();

    assert(showdownNode.state.totalWagers[Player::P0] == showdownNode.state.totalWagers[Player::P1]);
    int playerWagers = showdownNode.state.totalWagers[Player::P0];

    // Winner wins the other player's wager and the dead money
    // Loser loses their wager
    // If the players tie, they split the dead money
    float winPayoff = static_cast<float>(playerWagers + tree.deadMoney);
    float losePayoff = static_cast<float>(-playerWagers);
    float tiePayoff = static_cast<float>(tree.deadMoney) / 2.0f;

    // First pass: Calculate hero winning hands
    {
        float villainTotalReachProb = 0.0f;
        std::array<float, StandardDeckSize> villainReachProbWithCard = {};

        int villainIndexSorted = 0;

        for (HandData heroHandData : sortedHandRanks[hero]) {
            assert(areHandAndSetDisjoint(hero, heroHandData.index, showdownNode.state.currentBoard, tree));

            while (villainIndexSorted < villainFilteredRangeSize && sortedHandRanks[villain][villainIndexSorted].rank < heroHandData.rank) {
                int villainHandIndex = sortedHandRanks[villain][villainIndexSorted].index;
                assert(areHandAndSetDisjoint(villain, villainHandIndex, showdownNode.state.currentBoard, tree));

                float villainReachProb = villainReachProbs[villainHandIndex];
                villainTotalReachProb += villainReachProb;
                addReachProbsToArray(villainReachProbWithCard, villainHandIndex, villainReachProb, constants, tree);

                ++villainIndexSorted;
            }

            if (villainTotalReachProb == 0.0f) {
                continue;
            }

            float villainValidReachProb = villainTotalReachProb
                - getReachProbBlockedByHeroHand(heroHandData.index, villainReachProbWithCard, constants, villainReachProbs, tree);
            // We don't need to call getInclusionExculsionCorrection because this pass only includes cases where the hero wins
            // If the hero and villain hands were identical, then it would be a tie
            // Thus getReachProbBlockedByHeroHand will not have any contribution from identical hero and villain hands

            outputExpectedValues[heroHandData.index] += winPayoff * villainValidReachProb;
        }
    }

    // Second pass: Calculate hero losing hands
    {
        float villainTotalReachProb = 0.0f;
        std::array<float, StandardDeckSize> villainReachProbWithCard = {};

        int villainIndexSorted = villainFilteredRangeSize - 1;

        for (HandData heroHandData : std::views::reverse(sortedHandRanks[hero])) {
            assert(areHandAndSetDisjoint(hero, heroHandData.index, showdownNode.state.currentBoard, tree));

            while (villainIndexSorted >= 0 && sortedHandRanks[villain][villainIndexSorted].rank > heroHandData.rank) {
                int villainHandIndex = sortedHandRanks[villain][villainIndexSorted].index;
                assert(areHandAndSetDisjoint(villain, villainHandIndex, showdownNode.state.currentBoard, tree));

                float villainReachProb = villainReachProbs[villainHandIndex];
                villainTotalReachProb += villainReachProb;
                addReachProbsToArray(villainReachProbWithCard, villainHandIndex, villainReachProb, constants, tree);

                --villainIndexSorted;
            }

            if (villainTotalReachProb == 0.0f) {
                continue;
            }

            float villainValidReachProb = villainTotalReachProb
                - getReachProbBlockedByHeroHand(heroHandData.index, villainReachProbWithCard, constants, villainReachProbs, tree);
            // We don't need to call getInclusionExculsionCorrection because this pass only includes cases where the hero loses
            // If the hero and villain hands were identical, then it would be a tie
            // Thus getReachProbBlockedByHeroHand will not have any contribution from identical hero and villain hands

            outputExpectedValues[heroHandData.index] += losePayoff * villainValidReachProb;
        }
    }

    // Third pass: Calculate tie hands
    // Can ignore ties in zero-sum game, 0 EV for both players
    if (tree.deadMoney > 0) {
        float villainTotalReachProb = 0.0f;
        std::array<float, StandardDeckSize> villainReachProbWithCard = {};

        int villainIndexSorted = 0;

        for (int heroIndexSorted = 0; heroIndexSorted < heroFilteredRangeSize; ++heroIndexSorted) {
            HandData heroHandData = sortedHandRanks[hero][heroIndexSorted];
            assert(areHandAndSetDisjoint(hero, heroHandData.index, showdownNode.state.currentBoard, tree));

            bool heroRankIncreased = (heroIndexSorted == 0) || (heroHandData.rank > sortedHandRanks[hero][heroIndexSorted - 1].rank);
            if (heroRankIncreased) {
                // We need to reset our reach probs because the hero's rank has increased
                villainTotalReachProb = 0.0;
                villainReachProbWithCard.fill(0.0);

                // Skip until we find a hand that we tie with
                while (villainIndexSorted < villainFilteredRangeSize && sortedHandRanks[villain][villainIndexSorted].rank < heroHandData.rank) {
                    ++villainIndexSorted;
                }

                while (villainIndexSorted < villainFilteredRangeSize && sortedHandRanks[villain][villainIndexSorted].rank == heroHandData.rank) {
                    int villainHandIndex = sortedHandRanks[villain][villainIndexSorted].index;
                    assert(areHandAndSetDisjoint(villain, villainHandIndex, showdownNode.state.currentBoard, tree));

                    float villainReachProb = villainReachProbs[villainHandIndex];
                    villainTotalReachProb += villainReachProb;
                    addReachProbsToArray(villainReachProbWithCard, villainHandIndex, villainReachProb, constants, tree);

                    ++villainIndexSorted;
                }
            }

            if (villainTotalReachProb == 0.0f) {
                continue;
            }

            float villainValidReachProb = villainTotalReachProb
                - getReachProbBlockedByHeroHand(heroHandData.index, villainReachProbWithCard, constants, villainReachProbs, tree)
                + getInclusionExculsionCorrection(heroHandData.index, constants, villainReachProbs, tree);

            outputExpectedValues[heroHandData.index] += tiePayoff * villainValidReachProb;
        }
    }
}

void traverseTree(
    const Node& node,
    const TraversalConstants& constants,
    const IGameRules& rules,
    std::span<const float> villainReachProbs,
    std::span<float> outputExpectedValues,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    assert(tree.isTreeSkeletonBuilt() && tree.areCfrVectorsInitialized());

    switch (node.nodeType) {
        case NodeType::Chance:
            traverseChance(node, constants, rules, villainReachProbs, outputExpectedValues, tree, allocator);
            break;
        case NodeType::Decision:
            traverseDecision(node, constants, rules, villainReachProbs, outputExpectedValues, tree, allocator);
            break;
        case NodeType::Fold:
            traverseFold(node, constants, villainReachProbs, outputExpectedValues, tree);
            break;
        case NodeType::Showdown:
            traverseShowdown(node, constants, rules, villainReachProbs, outputExpectedValues, tree);
            break;
        default:
            assert(false);
            break;
    }
}

void traverseFromRoot(const TraversalConstants& constants, const IGameRules& rules, std::span<float> outputExpectedValues, Tree& tree, StackAllocator<float>& allocator) {
    Player villain = getOpposingPlayer(constants.hero);
    const auto& initialRangeWeights = rules.getInitialRangeWeights(villain);

    int heroRangeSize = tree.rangeSize[constants.hero];
    int villainRangeSize = tree.rangeSize[villain];

    ScopedVector<float> villainReachProbs(allocator, getThreadIndex(), villainRangeSize);
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
    ScopedVector<float> outputExpectedValues(allocator, getThreadIndex(), heroRangeSize);

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

    ScopedVector<float> outputExpectedValues(allocator, getThreadIndex(), tree.rangeSize[hero]);
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

    ScopedVector<float> outputExpectedValues(allocator, getThreadIndex(), tree.rangeSize[hero]);
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

    ScopedVector<float> outputExpectedValues(allocator, getThreadIndex(), tree.rangeSize[hero]);
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

// TODO: This is basically the same as writeAverageStrategyToBuffer
FixedVector<float, MaxNumActions> getFinalStrategy(int hand, const Node& decisionNode, const Tree& tree) {
    assert(decisionNode.nodeType == NodeType::Decision);

    int playerToActRangeSize = tree.rangeSize[decisionNode.state.playerToAct];
    int numActions = static_cast<int>(decisionNode.numChildren);
    assert(numActions > 0);

    float total = 0.0f;
    for (int action = 0; action < numActions; ++action) {
        total += tree.allStrategySums[getTrainingDataActionOffset(action, decisionNode, tree) + hand];
    }

    if (total > 0.0f) {
        FixedVector<float, MaxNumActions> finalStrategy(numActions);
        for (int action = 0; action < numActions; ++action) {
            finalStrategy[action] = tree.allStrategySums[getTrainingDataActionOffset(action, decisionNode, tree) + hand] / total;
        }
        return finalStrategy;
    }
    else {
        // Play a uniform strategy if we don't have a strategy yet
        assert(total == 0.0f);
        return FixedVector<float, MaxNumActions>(numActions, 1.0f / static_cast<float>(numActions));
    }
}