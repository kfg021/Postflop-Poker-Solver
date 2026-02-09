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

void writeCurrentStrategyToBuffer(std::span<float> currentStrategyBuffer, const Node& decisionNode, const Tree& tree, StackAllocator<float>& allocator) {
    assert(decisionNode.nodeType == NodeType::Decision);

    int numActions = decisionNode.numChildren;
    int playerToActRangeSize = tree.rangeSize[decisionNode.state.playerToAct];
    assert(numActions > 0);
    assert(currentStrategyBuffer.size() == numActions * playerToActRangeSize);

    ScopedVector<float> totalPositiveRegrets(allocator, getThreadIndex(), playerToActRangeSize);
    std::fill(totalPositiveRegrets.begin(), totalPositiveRegrets.end(), 0.0f);

    const auto regretSums = tree.allRegretSums.begin() + decisionNode.trainingDataOffset;

    for (int action = 0; action < numActions; ++action) {
        for (int hand = 0; hand < playerToActRangeSize; ++hand) {
            float positiveRegret = std::max(regretSums[action * playerToActRangeSize + hand], 0.0f);
            currentStrategyBuffer[action * playerToActRangeSize + hand] = positiveRegret;
            totalPositiveRegrets[hand] += positiveRegret;
        }
    }

    float numActionsInverse = 1.0f / static_cast<float>(numActions);
    for (int action = 0; action < numActions; ++action) {
        for (int hand = 0; hand < playerToActRangeSize; ++hand) {
            if (totalPositiveRegrets[hand] > 0.0f) {
                currentStrategyBuffer[action * playerToActRangeSize + hand] /= totalPositiveRegrets[hand];
            }
            else {
                // Play a uniform strategy if no action has positive regret
                assert(totalPositiveRegrets[hand] == 0.0f);
                currentStrategyBuffer[action * playerToActRangeSize + hand] = numActionsInverse;
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

    const auto strategySums = tree.allStrategySums.begin() + decisionNode.trainingDataOffset;

    for (int action = 0; action < numActions; ++action) {
        for (int hand = 0; hand < playerToActRangeSize; ++hand) {
            float strategy = strategySums[action * playerToActRangeSize + hand];
            assert(strategy >= 0.0f);
            averageStrategyBuffer[action * playerToActRangeSize + hand] = strategy;
            totalStrategy[hand] += strategy;
        }
    }

    float numActionsInverse = 1.0f / static_cast<float>(numActions);
    for (int action = 0; action < numActions; ++action) {
        for (int hand = 0; hand < playerToActRangeSize; ++hand) {
            if (totalStrategy[hand] > 0.0f) {
                averageStrategyBuffer[action * playerToActRangeSize + hand] /= totalStrategy[hand];
            }
            else {
                // Play a uniform strategy if we don't have a strategy yet
                assert(totalStrategy[hand] == 0.0f);
                averageStrategyBuffer[action * playerToActRangeSize + hand] = numActionsInverse;
            }
        }
    }
}

template <int GameHandSize>
bool areHandAndCardDisjoint(HandInfo playerHandInfo, CardID card) {
    static_assert(GameHandSize == 1 || GameHandSize == 2);
    if constexpr (GameHandSize == 1) {
        assert(playerHandInfo.card0 != InvalidCard);
        assert(playerHandInfo.card1 == InvalidCard);
        return playerHandInfo.card0 != card;
    }
    else if constexpr (GameHandSize == 2) {
        assert(playerHandInfo.card0 != InvalidCard);
        assert(playerHandInfo.card1 != InvalidCard);
        return (playerHandInfo.card0 != card) && (playerHandInfo.card1 != card);
    }
}

template <int GameHandSize>
bool areHandAndSetDisjoint(HandInfo playerHandInfo, CardSet cardSet) {
    static_assert(GameHandSize == 1 || GameHandSize == 2);
    if constexpr (GameHandSize == 1) {
        assert(playerHandInfo.card0 != InvalidCard);
        assert(playerHandInfo.card1 == InvalidCard);
        return !setContainsCard(cardSet, playerHandInfo.card0);
    }
    else if constexpr (GameHandSize == 2) {
        assert(playerHandInfo.card0 != InvalidCard);
        assert(playerHandInfo.card1 != InvalidCard);
        return !setContainsCard(cardSet, playerHandInfo.card0) && !setContainsCard(cardSet, playerHandInfo.card1);
    }
}

template <int GameHandSize>
void addReachProbsToArray(
    std::array<float, StandardDeckSize>& villainReachProbWithCard,
    HandInfo villainHandInfo,
    float villainReachProb
) {
    static_assert(GameHandSize == 1 || GameHandSize == 2);
    if constexpr (GameHandSize == 1) {
        assert(villainHandInfo.card0 != InvalidCard);
        assert(villainHandInfo.card1 == InvalidCard);
        villainReachProbWithCard[villainHandInfo.card0] += villainReachProb;
    }
    else if constexpr (GameHandSize == 2) {
        assert(villainHandInfo.card0 != InvalidCard);
        assert(villainHandInfo.card1 != InvalidCard);
        villainReachProbWithCard[villainHandInfo.card0] += villainReachProb;
        villainReachProbWithCard[villainHandInfo.card1] += villainReachProb;
    }
}

template <int GameHandSize>
float getReachProbBlockedByHeroHand(
    HandInfo heroHandInfo,
    const std::array<float, StandardDeckSize>& villainReachProbWithCard
) {
    static_assert(GameHandSize == 1 || GameHandSize == 2);
    if constexpr (GameHandSize == 1) {
        assert(heroHandInfo.card0 != InvalidCard);
        assert(heroHandInfo.card1 == InvalidCard);
        return villainReachProbWithCard[heroHandInfo.card0];
    }
    else if constexpr (GameHandSize == 2) {
        assert(heroHandInfo.card0 != InvalidCard);
        assert(heroHandInfo.card1 != InvalidCard);
        return villainReachProbWithCard[heroHandInfo.card0] + villainReachProbWithCard[heroHandInfo.card1];
    }
}

// Returns the portion of the villain's range that was double-counted in the above function
template <int GameHandSize>
float getInclusionExculsionCorrection(
    int heroHandIndex,
    std::span<const float> villainReachProbs,
    const std::vector<std::int16_t>& heroSameHandIndexTable
) {
    static_assert(GameHandSize == 1 || GameHandSize == 2);
    if constexpr (GameHandSize == 1) {
        return 0.0f;
    }
    else if constexpr (GameHandSize == 2) {
        std::int16_t sameHandIndex = heroSameHandIndexTable[heroHandIndex];
        return (sameHandIndex != -1) ? villainReachProbs[sameHandIndex] : 0.0f;
    }
    // For hand size > 2, logic is more complicated
}

template <int GameHandSize, TraversalMode Mode>
void traverseTree(
    const Node& node,
    const TraversalConstants& constants,
    const IGameRules& rules,
    std::span<const float> villainReachProbs,
    std::span<float> outputExpectedValues,
    Tree& tree,
    StackAllocator<float>& allocator
);

template <int GameHandSize, TraversalMode Mode>
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
        assert(chanceCard != InvalidCard);

        // Normalize expected values by the number of total chance cards possible
        // Hero and villain both have a hand
        int chanceCardReachFactor = getSetSize(chanceNode.availableCards) - (2 * GameHandSize);

        const auto villainValidHands = rules.getValidHands(villain, nextNode.state.currentBoard);

        ScopedVector<float> newVillainReachProbs(allocator, getThreadIndex(), villainRangeSize);
        std::fill(newVillainReachProbs.begin(), newVillainReachProbs.end(), 0.0f);

        for (HandInfo villainHandInfo : villainValidHands) {
            assert(villainHandInfo != InvalidHand);
            assert(areHandAndCardDisjoint<GameHandSize>(villainHandInfo, chanceCard));
            newVillainReachProbs[villainHandInfo.index] = villainReachProbs[villainHandInfo.index] / static_cast<float>(chanceCardReachFactor);
        }

        auto evCardRangeBegin = newOutputExpectedValues.begin() + cardIndex * heroRangeSize;
        auto evCardRangeEnd = evCardRangeBegin + heroRangeSize;
        traverseTree<GameHandSize, Mode>(nextNode, constants, rules, newVillainReachProbs.getData(), { evCardRangeBegin, evCardRangeEnd }, tree, allocator);
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
        // Used in debug mode only
        auto getHandInfo = [&rules](Player player, int handIndex) -> HandInfo {
            CardSet hand = rules.getRangeHands(player)[handIndex];

            CardID card0;
            CardID card1;
            static_assert(GameHandSize == 1 || GameHandSize == 2);
            if constexpr (GameHandSize == 1) {
                card0 = popLowestCardFromSet(hand);
                card1 = InvalidCard;
            }
            else if constexpr (GameHandSize == 2) {
                card0 = popLowestCardFromSet(hand);
                card1 = popLowestCardFromSet(hand);
            }
            assert(hand == 0);

            return { .index = static_cast<std::int16_t>(handIndex), .card0 = card0, .card1 = card1 };
        };

        CardID chanceCard = tree.allNodes[chanceNode.childrenOffset + cardIndex].state.lastDealtCard;

        // First calculate contribution from canonical cards
        for (int hand = 0; hand < heroRangeSize; ++hand) {
            // Because of how fold and showdown nodes are structured, blocked hands will always return 0.0f.
            // Therefore, we can just add them directly and avoid having to branch
            assert(
                areHandAndCardDisjoint<GameHandSize>(getHandInfo(constants.hero, hand), chanceCard)
                || newOutputExpectedValues[cardIndex * heroRangeSize + hand] == 0.0f
            );
            outputExpectedValues[hand] += newOutputExpectedValues[cardIndex * heroRangeSize + hand];
        }

        // Then calculate contribution from all isomorphisms
        for (SuitMapping mapping : chanceNode.suitMappings) {
            assert(mapping.parent != mapping.child);

            if (mapping.parent == getCardSuit(chanceCard)) {
                CardID isomorphicCard = getCardIDFromValueAndSuit(getCardValue(chanceCard), mapping.child);
                const auto& isomorphicHandIndices = tree.isomorphicHandIndices[constants.hero][mapTwoSuitsToIndex(mapping.parent, mapping.child)];
                assert(isomorphicHandIndices.size() == heroRangeSize);

                for (int hand = 0; hand < heroRangeSize; ++hand) {
                    std::int16_t isomorphicHand = isomorphicHandIndices[hand];
                    assert(isomorphicHand != -1);

                    // Because of how fold and showdown nodes are structured, blocked hands will always return 0.0f.
                    // Therefore, we can just add them directly and avoid having to branch
                    assert(
                        areHandAndCardDisjoint<GameHandSize>(getHandInfo(constants.hero, hand), isomorphicCard)
                        || newOutputExpectedValues[cardIndex * heroRangeSize + isomorphicHand] == 0.0f
                    );
                    outputExpectedValues[hand] += newOutputExpectedValues[cardIndex * heroRangeSize + isomorphicHand];
                }
            }
        }
    }
}

template <int GameHandSize, TraversalMode Mode>
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
            traverseTree<GameHandSize, Mode>(tree.allNodes[decisionNode.childrenOffset + action], constants, rules, villainReachProbs, { evActionRangeBegin, evActionRangeEnd }, tree, allocator);
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
            traverseTree<GameHandSize, Mode>(tree.allNodes[decisionNode.childrenOffset + action], constants, rules, newVillainReachProbs.getData(), { evActionRangeBegin, evActionRangeEnd }, tree, allocator);
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

        ScopedVector<float> newOutputExpectedValues(allocator, getThreadIndex(), numActions * heroRangeSize);
        calculateActionEVs(newOutputExpectedValues.getData(), {});

        // Calculate expected value of strategy
        for (int action = 0; action < numActions; ++action) {
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                outputExpectedValues[hand] += newOutputExpectedValues[action * heroRangeSize + hand] * currentStrategy[action * heroRangeSize + hand];
            }
        }

        // Regret and strategy updates
        auto regretSums = tree.allRegretSums.begin() + decisionNode.trainingDataOffset;
        auto strategySums = tree.allStrategySums.begin() + decisionNode.trainingDataOffset;

        for (int action = 0; action < numActions; ++action) {
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                float& regretSum = regretSums[action * heroRangeSize + hand];
                float& strategySum = strategySums[action * heroRangeSize + hand];

                float strategyExpectedValue = outputExpectedValues[hand];
                float actionExpectedValue = newOutputExpectedValues[action * heroRangeSize + hand];
                float regret = actionExpectedValue - strategyExpectedValue;

                // TODO: Do we need to weight by hero reach probs?
                float strategy = currentStrategy[action * heroRangeSize + hand];

                if constexpr (Mode == TraversalMode::VanillaCfr) {
                    regretSum += regret;
                    strategySum += strategy;
                }
                else if constexpr (Mode == TraversalMode::CfrPlus) {
                    // In CFR+, we erase negative regrets
                    regretSum += std::max(regret, 0.0f);
                    strategySum += strategy;
                }
                else if constexpr (Mode == TraversalMode::DiscountedCfr) {
                    // In DCFR, we discount previous regrets and strategies by a factor
                    float alpha = constants.params.alphaT;
                    float beta = constants.params.betaT;
                    float gamma = constants.params.gammaT;

                    float regretDiscount = (regretSum > 0.0f) ? alpha : beta;
                    regretSum = regretSum * regretDiscount + regret;
                    strategySum = strategySum * gamma + strategy;
                }
            }
        }
    };

    auto heroToActExpectedValue = [
        &decisionNode,
        &constants,
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

        if constexpr (Mode == TraversalMode::VanillaCfr
            || Mode == TraversalMode::CfrPlus
            || Mode == TraversalMode::DiscountedCfr) {
            writeCurrentStrategyToBuffer(strategy.getData(), decisionNode, tree, allocator);
        }
        else {
            writeAverageStrategyToBuffer(strategy.getData(), decisionNode, tree, allocator);
        }

        ScopedVector<float> newOutputExpectedValues(allocator, getThreadIndex(), numActions * heroRangeSize);
        calculateActionEVs(newOutputExpectedValues.getData(), strategy.getData());

        // Calculate expected value of strategy
        // Not the hero's turn; no strategy or regret updates
        for (int action = 0; action < numActions; ++action) {
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                outputExpectedValues[hand] += newOutputExpectedValues[action * heroRangeSize + hand];
            }
        }
    };

    if (constants.hero == decisionNode.state.playerToAct) {
        if constexpr (Mode == TraversalMode::VanillaCfr
            || Mode == TraversalMode::CfrPlus
            || Mode == TraversalMode::DiscountedCfr) {
            heroToActTraining();
        }
        else if constexpr (Mode == TraversalMode::ExpectedValue) {
            heroToActExpectedValue();
        }
        else if constexpr (Mode == TraversalMode::BestResponse) {
            heroToActBestResponse();
        }
    }
    else {
        villainToAct();
    }
}

template <int GameHandSize, TraversalMode Mode>
void traverseFold(
    const Node& foldNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    std::span<const float> villainReachProbs,
    std::span<float> outputExpectedValues,
    Tree& tree
) {
    assert(foldNode.nodeType == NodeType::Fold);

    std::fill(outputExpectedValues.begin(), outputExpectedValues.end(), 0.0f);

    Player villain = getOpposingPlayer(constants.hero);

    const auto heroValidHands = rules.getValidHands(constants.hero, foldNode.state.currentBoard);
    const auto villainValidHands = rules.getValidHands(villain, foldNode.state.currentBoard);

    float villainTotalReachProb = 0.0f;
    std::array<float, StandardDeckSize> villainReachProbWithCard = {};

    for (HandInfo villainHandInfo : villainValidHands) {
        assert(villainHandInfo != InvalidHand);
        assert(areHandAndSetDisjoint<GameHandSize>(villainHandInfo, foldNode.state.currentBoard));

        float villainReachProb = villainReachProbs[villainHandInfo.index];
        villainTotalReachProb += villainReachProb;
        addReachProbsToArray<GameHandSize>(villainReachProbWithCard, villainHandInfo, villainReachProb);
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

    const auto& heroSameHandIndexTable = tree.sameHandIndexTable[constants.hero];

    for (HandInfo heroHandInfo : heroValidHands) {
        assert(heroHandInfo != InvalidHand);
        assert(areHandAndSetDisjoint<GameHandSize>(heroHandInfo, foldNode.state.currentBoard));

        float villainValidReachProb = villainTotalReachProb
            - getReachProbBlockedByHeroHand<GameHandSize>(heroHandInfo, villainReachProbWithCard)
            + getInclusionExculsionCorrection<GameHandSize>(heroHandInfo.index, villainReachProbs, heroSameHandIndexTable);

        outputExpectedValues[heroHandInfo.index] += heroPayoff * villainValidReachProb;
    }
}

template <int GameHandSize, TraversalMode Mode>
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

    const auto heroSortedHandRanks = rules.getValidSortedHandRanks(hero, showdownNode.state.currentBoard);
    const auto villainSortedHandRanks = rules.getValidSortedHandRanks(villain, showdownNode.state.currentBoard);

    int heroFilteredRangeSize = heroSortedHandRanks.size();
    int villainFilteredRangeSize = villainSortedHandRanks.size();

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

        for (RankedHand heroRankedHand : heroSortedHandRanks) {
            assert(areHandAndSetDisjoint<GameHandSize>(heroRankedHand.info, showdownNode.state.currentBoard));

            while (villainIndexSorted < villainFilteredRangeSize && villainSortedHandRanks[villainIndexSorted].rank < heroRankedHand.rank) {
                RankedHand villainRankedHand = villainSortedHandRanks[villainIndexSorted];
                assert(areHandAndSetDisjoint<GameHandSize>(villainRankedHand.info, showdownNode.state.currentBoard));

                float villainReachProb = villainReachProbs[villainRankedHand.info.index];
                villainTotalReachProb += villainReachProb;
                addReachProbsToArray<GameHandSize>(villainReachProbWithCard, villainRankedHand.info, villainReachProb);

                ++villainIndexSorted;
            }

            if (villainTotalReachProb == 0.0f) {
                continue;
            }

            float villainValidReachProb = villainTotalReachProb
                - getReachProbBlockedByHeroHand<GameHandSize>(heroRankedHand.info, villainReachProbWithCard);
            // We don't need to call getInclusionExculsionCorrection because this pass only includes cases where the hero wins
            // If the hero and villain hands were identical, then it would be a tie
            // Thus getReachProbBlockedByHeroHand will not have any contribution from identical hero and villain hands

            outputExpectedValues[heroRankedHand.info.index] += winPayoff * villainValidReachProb;
        }
    }

    // Second pass: Calculate hero losing hands
    {
        float villainTotalReachProb = 0.0f;
        std::array<float, StandardDeckSize> villainReachProbWithCard = {};

        int villainIndexSorted = villainFilteredRangeSize - 1;

        for (RankedHand heroRankedHand : std::views::reverse(heroSortedHandRanks)) {
            assert(areHandAndSetDisjoint<GameHandSize>(heroRankedHand.info, showdownNode.state.currentBoard));

            while (villainIndexSorted >= 0 && villainSortedHandRanks[villainIndexSorted].rank > heroRankedHand.rank) {
                RankedHand villainRankedHand = villainSortedHandRanks[villainIndexSorted];
                assert(areHandAndSetDisjoint<GameHandSize>(villainRankedHand.info, showdownNode.state.currentBoard));

                float villainReachProb = villainReachProbs[villainRankedHand.info.index];
                villainTotalReachProb += villainReachProb;
                addReachProbsToArray<GameHandSize>(villainReachProbWithCard, villainRankedHand.info, villainReachProb);

                --villainIndexSorted;
            }

            if (villainTotalReachProb == 0.0f) {
                continue;
            }

            float villainValidReachProb = villainTotalReachProb
                - getReachProbBlockedByHeroHand<GameHandSize>(heroRankedHand.info, villainReachProbWithCard);
            // We don't need to call getInclusionExculsionCorrection because this pass only includes cases where the hero loses
            // If the hero and villain hands were identical, then it would be a tie
            // Thus getReachProbBlockedByHeroHand will not have any contribution from identical hero and villain hands

            outputExpectedValues[heroRankedHand.info.index] += losePayoff * villainValidReachProb;
        }
    }

    // Third pass: Calculate tie hands
    // Can ignore ties in zero-sum game, 0 EV for both players
    if (tree.deadMoney > 0) {
        const auto& heroSameHandIndexTable = tree.sameHandIndexTable[hero];

        float villainTotalReachProb = 0.0f;
        std::array<float, StandardDeckSize> villainReachProbWithCard = {};

        int villainIndexSorted = 0;

        for (int heroIndexSorted = 0; heroIndexSorted < heroFilteredRangeSize; ++heroIndexSorted) {
            RankedHand heroRankedHand = heroSortedHandRanks[heroIndexSorted];
            assert(areHandAndSetDisjoint<GameHandSize>(heroRankedHand.info, showdownNode.state.currentBoard));

            bool heroRankIncreased = (heroIndexSorted == 0) || (heroRankedHand.rank > heroSortedHandRanks[heroIndexSorted - 1].rank);
            if (heroRankIncreased) {
                // We need to reset our reach probs because the hero's rank has increased
                villainTotalReachProb = 0.0;
                villainReachProbWithCard.fill(0.0);

                // Skip until we find a hand that we tie with
                while (villainIndexSorted < villainFilteredRangeSize && villainSortedHandRanks[villainIndexSorted].rank < heroRankedHand.rank) {
                    ++villainIndexSorted;
                }

                while (villainIndexSorted < villainFilteredRangeSize && villainSortedHandRanks[villainIndexSorted].rank == heroRankedHand.rank) {
                    RankedHand villainRankedHand = villainSortedHandRanks[villainIndexSorted];
                    assert(areHandAndSetDisjoint<GameHandSize>(villainRankedHand.info, showdownNode.state.currentBoard));

                    float villainReachProb = villainReachProbs[villainRankedHand.info.index];
                    villainTotalReachProb += villainReachProb;
                    addReachProbsToArray<GameHandSize>(villainReachProbWithCard, villainRankedHand.info, villainReachProb);

                    ++villainIndexSorted;
                }
            }

            if (villainTotalReachProb == 0.0f) {
                continue;
            }

            float villainValidReachProb = villainTotalReachProb
                - getReachProbBlockedByHeroHand<GameHandSize>(heroRankedHand.info, villainReachProbWithCard)
                + getInclusionExculsionCorrection<GameHandSize>(heroRankedHand.info.index, villainReachProbs, heroSameHandIndexTable);

            outputExpectedValues[heroRankedHand.info.index] += tiePayoff * villainValidReachProb;
        }
    }
}

template <int GameHandSize, TraversalMode Mode>
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
            traverseChance<GameHandSize, Mode>(node, constants, rules, villainReachProbs, outputExpectedValues, tree, allocator);
            break;
        case NodeType::Decision:
            traverseDecision<GameHandSize, Mode>(node, constants, rules, villainReachProbs, outputExpectedValues, tree, allocator);
            break;
        case NodeType::Fold:
            traverseFold<GameHandSize, Mode>(node, constants, rules, villainReachProbs, outputExpectedValues, tree);
            break;
        case NodeType::Showdown:
            traverseShowdown<GameHandSize, Mode>(node, constants, rules, villainReachProbs, outputExpectedValues, tree);
            break;
        default:
            assert(false);
            break;
    }
}

template <TraversalMode Mode>
void traverseFromRoot(const TraversalConstants& constants, const IGameRules& rules, std::span<float> outputExpectedValues, Tree& tree, StackAllocator<float>& allocator) {
    Player villain = getOpposingPlayer(constants.hero);
    const auto initialRangeWeights = rules.getInitialRangeWeights(villain);

    int heroRangeSize = tree.rangeSize[constants.hero];
    int villainRangeSize = tree.rangeSize[villain];

    ScopedVector<float> villainReachProbs(allocator, getThreadIndex(), villainRangeSize);
    for (int hand = 0; hand < villainRangeSize; ++hand) {
        villainReachProbs[hand] = initialRangeWeights[hand];
    }

    switch (tree.gameHandSize) {
        case 1: {
            traverseTree<1, Mode>(
                tree.allNodes[tree.getRootNodeIndex()],
                constants,
                rules,
                villainReachProbs,
                outputExpectedValues,
                tree,
                allocator
            );
            break;
        }

        case 2: {
            traverseTree<2, Mode>(
                tree.allNodes[tree.getRootNodeIndex()],
                constants,
                rules,
                villainReachProbs,
                outputExpectedValues,
                tree,
                allocator
            );
            break;
        }

        default:
            assert(false);
            break;
    }
}

template <TraversalMode Mode>
float rootExpectedValue(
    Player hero,
    const IGameRules& rules,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    static_assert((Mode == TraversalMode::ExpectedValue) || (Mode == TraversalMode::BestResponse));

    // Allocator should be empty before starting traversal, otherwise something wasn't deleted correctly
    assert(allocator.isEmpty());

    TraversalConstants constants = {
       .hero = hero,
       .params = {} // No params needed for expected value
    };

    int heroRangeSize = tree.rangeSize[hero];
    ScopedVector<float> outputExpectedValues(allocator, getThreadIndex(), heroRangeSize);

    traverseFromRoot<Mode>(constants, rules, outputExpectedValues, tree, allocator);

    const auto heroRangeWeights = rules.getInitialRangeWeights(hero);
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
        .alphaT = static_cast<float>(a / (a + 1)),
        .betaT = static_cast<float>(b / (b + 1)),
        .gammaT = static_cast<float>(std::pow(t / (t + 1), static_cast<double>(gamma)))
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
        .params = {} // No params needed for vanilla CFR
    };

    ScopedVector<float> outputExpectedValues(allocator, getThreadIndex(), tree.rangeSize[hero]);
    traverseFromRoot<TraversalMode::VanillaCfr>(constants, rules, outputExpectedValues, tree, allocator);
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
        .params = {} // No params needed for CFR+
    };

    ScopedVector<float> outputExpectedValues(allocator, getThreadIndex(), tree.rangeSize[hero]);
    traverseFromRoot<TraversalMode::CfrPlus>(constants, rules, outputExpectedValues, tree, allocator);
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
        .params = params
    };

    ScopedVector<float> outputExpectedValues(allocator, getThreadIndex(), tree.rangeSize[hero]);
    traverseFromRoot<TraversalMode::DiscountedCfr>(constants, rules, outputExpectedValues, tree, allocator);
}

float expectedValue(
    Player hero,
    const IGameRules& rules,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    return rootExpectedValue<TraversalMode::ExpectedValue>(hero, rules, tree, allocator);
}

float bestResponseEV(
    Player hero,
    const IGameRules& rules,
    Tree& tree,
    StackAllocator<float>& allocator
) {
    return rootExpectedValue<TraversalMode::BestResponse>(hero, rules, tree, allocator);
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

    const auto strategySums = tree.allStrategySums.begin() + decisionNode.trainingDataOffset;

    float total = 0.0f;
    for (int action = 0; action < numActions; ++action) {
        total += strategySums[action * playerToActRangeSize + hand];
    }

    if (total > 0.0f) {
        FixedVector<float, MaxNumActions> finalStrategy(numActions);
        for (int action = 0; action < numActions; ++action) {
            finalStrategy[action] = strategySums[action * playerToActRangeSize + hand] / total;
        }
        return finalStrategy;
    }
    else {
        // Play a uniform strategy if we don't have a strategy yet
        assert(total == 0.0f);
        return FixedVector<float, MaxNumActions>(numActions, 1.0f / static_cast<float>(numActions));
    }
}