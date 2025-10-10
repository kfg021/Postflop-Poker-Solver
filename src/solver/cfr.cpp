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
// TODO: Reduce / eliminate vector heap allocations

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

int getRangeSize(Player player, const IGameRules& rules) {
    return static_cast<int>(rules.getInitialRangeWeights(player).size());
}

std::size_t getTrainingDataIndex(int action, int hand, const IGameRules& rules, const DecisionNode& decisionNode, const Tree& tree) {
    assert(action >= 0 && action < decisionNode.decisionDataSize);

    int playerToActRangeSize = getRangeSize(decisionNode.player, rules);
    assert(hand >= 0 && hand < playerToActRangeSize);

    return decisionNode.trainingDataOffset + (action * playerToActRangeSize) + hand;
}

bool areHandsAndBoardDisjoint(int heroIndex, int villainIndex, CardSet board, Player hero, const IGameRules& rules) {
    CardSet heroHand = rules.mapIndexToHand(hero, heroIndex);
    CardSet villainHand = rules.mapIndexToHand(getOpposingPlayer(hero), villainIndex);

    int individualSize = getSetSize(heroHand) + getSetSize(villainHand) + getSetSize(board);
    int combinedSize = getSetSize(heroHand | villainHand | board);
    assert(individualSize >= combinedSize);

    return individualSize == combinedSize;
};

std::vector<std::vector<float>> getCurrentStrategy(const IGameRules& rules, const DecisionNode& decisionNode, const Tree& tree) {
    int playerToActRangeSize = getRangeSize(decisionNode.player, rules);
    int numActions = decisionNode.decisionDataSize;
    assert(numActions > 0);

    std::vector<std::vector<float>> currentStrategy(numActions, std::vector<float>(playerToActRangeSize));

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
                currentStrategy[action][hand] = 1.0f / numActions;
            }
        }
        else {
            for (int action = 0; action < numActions; ++action) {
                float regretSum = tree.allRegretSums[getTrainingDataIndex(action, hand, rules, decisionNode, tree)];
                if (regretSum > 0.0f) {
                    currentStrategy[action][hand] = regretSum / totalPositiveRegret;
                }
                else {
                    currentStrategy[action][hand] = 0.0f;
                }
            }
        }
    }

    return currentStrategy;
};

std::vector<float> traverseTree(
    const Node& node,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const std::vector<float>& villainReachProbs,
    Tree& tree
);

std::vector<float> traverseChance(
    const ChanceNode& chanceNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const std::vector<float>& villainReachProbs,
    Tree& tree
) {
    int heroRangeSize = getRangeSize(constants.hero, rules);
    std::vector<float> expectedValues(heroRangeSize, 0.0f);

    int numChanceCards = chanceNode.chanceDataSize;
    for (int i = 0; i < numChanceCards; ++i) {
        CardID chanceCard = tree.allChanceCards[chanceNode.chanceDataOffset + i];
        std::size_t nextNodeIndex = tree.allChanceNextNodeIndices[chanceNode.chanceDataOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());
        const Node& nextNode = tree.allNodes[nextNodeIndex];

        // TODO: Set impossible villain hands to 0 reach prob
        std::vector<float> chanceCardExpectedValues = traverseTree(nextNode, constants, rules, villainReachProbs, tree);
        for (int hand = 0; hand < heroRangeSize; ++hand) {
            expectedValues[hand] += chanceCardExpectedValues[hand];
        }
    }

    // TODO: This should be a function in the rules interface
    const auto& heroRangeHands = rules.getRangeHands(constants.hero);
    assert(!heroRangeHands.empty());
    int gameHandSize = getSetSize(heroRangeHands[0]);

    // Normalize expected values by the number of total chance cards possible
    // Hero and villain both have a hand
    int numPossibleChanceCards = numChanceCards - (2 * gameHandSize);

    for (int hand = 0; hand < heroRangeSize; ++hand) {
        // TODO: Check for impossible hero hands
        expectedValues[hand] /= static_cast<float>(numPossibleChanceCards);
    }

    return expectedValues;
}

// TODO: Consider template for traverseDecision
std::vector<float> traverseDecision(
    const DecisionNode& decisionNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const std::vector<float>& villainReachProbs,
    Tree& tree
) {
    auto heroToActTraining = [
        &decisionNode,
        &constants,
        &rules,
        &villainReachProbs,
        &tree
    ]() -> std::vector<float> {
        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        std::vector<std::vector<float>> strategies = getCurrentStrategy(rules, decisionNode, tree);

        int heroRangeSize = getRangeSize(constants.hero, rules);
        std::vector<float> expectedValues(heroRangeSize, 0.0f);
        assert(heroRangeSize == strategies[0].size());

        // Regret and strategy discounting for DCFR
        if (constants.mode == TraversalMode::DiscountedCfr) {
            for (int action = 0; action < numActions; ++action) {
                for (int hand = 0; hand < heroRangeSize; ++hand) {
                    std::size_t index = getTrainingDataIndex(action, hand, rules, decisionNode, tree);
                    float& regretSum = tree.allRegretSums[index];
                    float& strategySum = tree.allStrategySums[index];

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

            std::vector<float> actionExpectedValues = traverseTree(nextNode, constants, rules, villainReachProbs, tree);
            assert(actionExpectedValues.size() == heroRangeSize);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                expectedValues[hand] += actionExpectedValues[hand] * strategies[action][hand];

                // Regret update part 1 - add EV of action
                float& regretSum = tree.allRegretSums[getTrainingDataIndex(action, hand, rules, decisionNode, tree)];
                regretSum += actionExpectedValues[hand];
            }
        }

        // Regret update part 2 - subtract total EV
        // Strategy update
        for (int action = 0; action < numActions; ++action) {
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                std::size_t index = getTrainingDataIndex(action, hand, rules, decisionNode, tree);
                float& regretSum = tree.allRegretSums[index];
                float& strategySum = tree.allStrategySums[index];

                regretSum -= expectedValues[hand];

                // TODO: Do we need to weight by hero reach probs?
                strategySum += strategies[action][hand];

                // In CFR+, we erase negative regrets for faster convergence
                if (constants.mode == TraversalMode::CfrPlus) {
                    if (regretSum < 0.0f) {
                        regretSum = 0.0f;
                    }
                }
            }
        }

        return expectedValues;
    };

    auto heroToActExpectedValue = [
        &decisionNode,
        &constants,
        &rules,
        &villainReachProbs,
        &tree
    ]() -> std::vector<float> {
        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        std::vector<std::vector<float>> strategies = getAverageStrategy(rules, decisionNode, tree);

        int heroRangeSize = getRangeSize(constants.hero, rules);
        std::vector<float> expectedValues(heroRangeSize, 0.0f);
        assert(heroRangeSize == strategies[0].size());

        for (int action = 0; action < numActions; ++action) {
            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            std::vector<float> actionExpectedValues = traverseTree(nextNode, constants, rules, villainReachProbs, tree);
            assert(actionExpectedValues.size() == heroRangeSize);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                expectedValues[hand] += actionExpectedValues[hand] * strategies[action][hand];
            }
        }

        return expectedValues;
    };

    auto heroToActBestResponse = [
        &decisionNode,
        &constants,
        &rules,
        &villainReachProbs,
        &tree
    ]() -> std::vector<float> {
        // To calculate best response, hero plays the maximally exploitative pure strategy
        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        static constexpr float Infinity = std::numeric_limits<float>::infinity();
        int heroRangeSize = getRangeSize(constants.hero, rules);
        std::vector<float> expectedValues(heroRangeSize, -INFINITY);

        for (int action = 0; action < numActions; ++action) {
            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            std::vector<float> actionExpectedValues = traverseTree(nextNode, constants, rules, villainReachProbs, tree);
            assert(actionExpectedValues.size() == heroRangeSize);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                // For each action, play the action that leads to highest EV
                expectedValues[hand] = std::max(expectedValues[hand], actionExpectedValues[hand]);
            }
        }

        for (float f : expectedValues) {
            assert(f != -Infinity);
        }

        return expectedValues;
    };

    auto villainToAct = [
        &decisionNode,
        &constants,
        &rules,
        &villainReachProbs,
        &tree
    ]() -> std::vector<float> {
        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        Player villain = getOpposingPlayer(constants.hero);

        std::vector<std::vector<float>> strategies = getCurrentStrategy(rules, decisionNode, tree);

        int heroRangeSize = getRangeSize(constants.hero, rules);
        std::vector<float> expectedValues(heroRangeSize, 0.0f);

        int villainRangeSize = villainReachProbs.size();
        assert(villainRangeSize == getRangeSize(villain, rules));
        assert(villainRangeSize == strategies[0].size());

        // Not the hero's turn; no strategy or regret updates
        for (int action = 0; action < numActions; ++action) {
            std::vector<float> newVillainReachProbs(villainRangeSize);
            for (int hand = 0; hand < villainRangeSize; ++hand) {
                newVillainReachProbs[hand] = villainReachProbs[hand] * strategies[action][hand];
            }

            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            std::vector<float> actionExpectedValues = traverseTree(nextNode, constants, rules, newVillainReachProbs, tree);
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                expectedValues[hand] += actionExpectedValues[hand];
            }
        }

        return expectedValues;
    };

    if (constants.hero == decisionNode.player) {
        switch (constants.mode) {
            case TraversalMode::VanillaCfr:
            case TraversalMode::CfrPlus:
            case TraversalMode::DiscountedCfr:
                return heroToActTraining();
            case TraversalMode::ExpectedValue:
                return heroToActExpectedValue();
            case TraversalMode::BestResponse:
                return heroToActBestResponse();
            default:
                assert(false);
                break;
        }
    }
    else {
        return villainToAct();
    }
}

std::vector<float> traverseFold(
    const FoldNode& foldNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const std::vector<float>& villainReachProbs
) {
    Player villain = getOpposingPlayer(constants.hero);

    int heroRangeSize = getRangeSize(constants.hero, rules);
    int villainRangeSize = villainReachProbs.size();
    assert(villainRangeSize == getRangeSize(villain, rules));

    int heroReward = (foldNode.remainingPlayer == constants.hero) ? foldNode.remainingPlayerReward : -foldNode.remainingPlayerReward;

    std::vector<float> expectedValues(heroRangeSize, 0.0f);

    for (int i = 0; i < heroRangeSize; ++i) {
        for (int j = 0; j < villainRangeSize; ++j) {
            if (areHandsAndBoardDisjoint(i, j, foldNode.board, constants.hero, rules)) {
                expectedValues[i] += static_cast<float>(heroReward) * villainReachProbs[j];
            }
        }
    }

    return expectedValues;
}

std::vector<float> traverseShowdown(
    const ShowdownNode& showdownNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const std::vector<float>& villainReachProbs
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

    int heroRangeSize = getRangeSize(constants.hero, rules);
    int villainRangeSize = villainReachProbs.size();
    assert(villainRangeSize == getRangeSize(villain, rules));

    std::vector<float> expectedValues(heroRangeSize, 0.0f);

    for (int i = 0; i < heroRangeSize; ++i) {
        for (int j = 0; j < villainRangeSize; ++j) {
            if (areHandsAndBoardDisjoint(i, j, showdownNode.board, constants.hero, rules)) {
                float heroReward = static_cast<float>(getMultiplier(i, j)) * static_cast<float>(showdownNode.reward);
                expectedValues[i] += heroReward * villainReachProbs[j];
            }
        }
    }

    return expectedValues;
}

std::vector<float> traverseTree(
    const Node& node,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const std::vector<float>& villainReachProbs,
    Tree& tree
) {
    assert(tree.isTreeSkeletonBuilt() && tree.isFullTreeBuilt());

    switch (node.getNodeType()) {
        case NodeType::Chance:
            return traverseChance(node.chanceNode, constants, rules, villainReachProbs, tree);
        case NodeType::Decision:
            return traverseDecision(node.decisionNode, constants, rules, villainReachProbs, tree);
        case NodeType::Fold:
            return traverseFold(node.foldNode, constants, rules, villainReachProbs);
        case NodeType::Showdown:
            return traverseShowdown(node.showdownNode, constants, rules, villainReachProbs);
        default:
            assert(false);
            return {};
    }
}

std::vector<float> traverseFromRoot(const TraversalConstants& constants, const IGameRules& rules, Tree& tree) {
    std::size_t rootNodeIndex = tree.getRootNodeIndex();
    std::vector<float> villainReachProbs = rules.getInitialRangeWeights(getOpposingPlayer(constants.hero));
    return traverseTree(
        tree.allNodes[rootNodeIndex],
        constants,
        rules,
        villainReachProbs,
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

        CardSet startingBoard = rules.getInitialGameState().currentBoard;

        float totalRangeWeight = 0.0f;

        for (int i = 0; i < heroRangeSize; ++i) {
            for (int j = 0; j < villainRangeSize; ++j) {
                if (areHandsAndBoardDisjoint(i, j, startingBoard, hero, rules)) {
                    totalRangeWeight += heroRangeWeights[i] * villainRangeWeights[j];
                }
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

    std::vector<float> expectedValueRange = traverseFromRoot(constants, rules, tree);
    const auto& heroRangeWeights = rules.getInitialRangeWeights(hero);
    assert(expectedValueRange.size() == heroRangeWeights.size());

    float expectedValue = 0.0f;
    for (int i = 0; i < expectedValueRange.size(); ++i) {
        expectedValue += expectedValueRange[i] * heroRangeWeights[i];
    }

    static float totalRangeWeight = getTotalRangeWeight();
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

    static_cast<void>(traverseFromRoot(constants, rules, tree));
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

    static_cast<void>(traverseFromRoot(constants, rules, tree));
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

    static_cast<void>(traverseFromRoot(constants, rules, tree));
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

std::vector<std::vector<float>> getAverageStrategy(const IGameRules& rules, const DecisionNode& decisionNode, const Tree& tree) {
    int playerToActRangeSize = getRangeSize(decisionNode.player, rules);
    int numActions = decisionNode.decisionDataSize;
    assert(numActions > 0);

    std::vector<std::vector<float>> averageStrategy(numActions, std::vector<float>(playerToActRangeSize));

    for (int hand = 0; hand < playerToActRangeSize; ++hand) {
        float total = 0.0f;
        for (int action = 0; action < numActions; ++action) {
            float strategySum = tree.allStrategySums[getTrainingDataIndex(action, hand, rules, decisionNode, tree)];
            total += strategySum;
        }

        if (total == 0.0f) {
            // Uniform strategy
            for (int action = 0; action < numActions; ++action) {
                averageStrategy[action][hand] = 1.0f / numActions;
            }
        }
        else {
            for (int action = 0; action < numActions; ++action) {
                float strategySum = tree.allStrategySums[getTrainingDataIndex(action, hand, rules, decisionNode, tree)];
                averageStrategy[action][hand] = strategySum / total;
            }
        }
    }

    return averageStrategy;
}