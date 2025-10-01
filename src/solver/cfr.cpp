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

std::size_t getTrainingDataIndex(int action, int hand, const IGameRules& rules, const DecisionNode& decisionNode, const Tree& tree) {
    assert(action >= 0 && action < decisionNode.decisionDataSize);

    int playerToActRangeSize = rules.getInitialRangeWeights(decisionNode.player).size();
    assert(hand >= 0 && hand < playerToActRangeSize);

    return decisionNode.trainingDataOffset + (action * playerToActRangeSize) + hand;
}

bool areHandsDisjoint(int heroIndex, int villainIndex, const TraversalConstants& constants, const IGameRules& rules) {
    CardSet heroHand = rules.mapIndexToHand(constants.hero, heroIndex);
    CardSet villainHand = rules.mapIndexToHand(getOpposingPlayer(constants.hero), villainIndex);

    int individualSize = getSetSize(heroHand) + getSetSize(villainHand);
    int combinedSize = getSetSize(heroHand | villainHand);
    assert(individualSize >= combinedSize);

    return individualSize == combinedSize;
};

std::vector<std::vector<float>> getCurrentStrategy(const IGameRules& rules, const DecisionNode& decisionNode, const Tree& tree) {
    int playerToActRangeSize = rules.getInitialRangeWeights(decisionNode.player).size();
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
    const PlayerArray<std::vector<float>>& rangeWeights,
    Tree& tree
);

std::vector<float> traverseChance(
    const ChanceNode& chanceNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::vector<float>>& rangeWeights,
    Tree& tree
) {
    // TODO: The probability that a particular card comes on the turn/river is influenced by the ranges of the players

    Player hero = constants.hero;

    int heroRangeSize = rangeWeights[hero].size();
    std::vector<float> expectedValues(heroRangeSize, 0.0f);

    int numChanceCards = chanceNode.chanceDataSize;
    for (int i = 0; i < numChanceCards; ++i) {
        CardID chanceCard = tree.allChanceCards[chanceNode.chanceDataOffset + i];
        std::size_t nextNodeIndex = tree.allChanceNextNodeIndices[chanceNode.chanceDataOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());
        const Node& nextNode = tree.allNodes[nextNodeIndex];

        PlayerArray<std::vector<float>> newRangeWeights = rangeWeights;
        for (Player player : { Player::P0, Player::P1 }) {
            for (int hand = 0; hand < newRangeWeights[player].size(); ++hand) {
                CardSet playerHand = rules.getRangeHands(player)[hand];
                if (setContainsCard(playerHand, chanceCard)) {
                    // The chance card we are about to deal conflicts with a hand in the player's range
                    newRangeWeights[player][hand] = 0.0f;
                }
                // TODO: Do we need to divide the ranges?
            }
        }

        std::vector<float> chanceCardExpectedValues = traverseTree(nextNode, constants, rules, newRangeWeights, tree);
        assert(chanceCardExpectedValues.size() == heroRangeSize);

        for (int hand = 0; hand < heroRangeSize; ++hand) {
            expectedValues[hand] += chanceCardExpectedValues[hand];
        }
    }

    // Normalize expected values
    for (int hand = 0; hand < heroRangeSize; ++hand) {
        expectedValues[hand] /= static_cast<float>(numChanceCards);
    }

    return expectedValues;
}

// TODO: Consider template for traverseDecision
std::vector<float> traverseDecision(
    const DecisionNode& decisionNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::vector<float>>& rangeWeights,
    Tree& tree
) {
    auto heroToActTraining = [
        &decisionNode,
        &constants,
        &rules,
        &rangeWeights,
        &tree
    ]() -> std::vector<float> {
        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        Player hero = constants.hero;
        Player villain = getOpposingPlayer(hero);

        std::vector<std::vector<float>> strategies = getCurrentStrategy(rules, decisionNode, tree);

        int heroRangeSize = rangeWeights[hero].size();
        std::vector<float> expectedValues(heroRangeSize, 0.0f);
        assert(heroRangeSize == strategies[0].size());

        // Calculate villain reach sum - used to weight regrets
        float villainReachSum = 0.0f;
        for (float f : rangeWeights[villain]) {
            villainReachSum += f;
        }

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
            PlayerArray<std::vector<float>> newRangeWeights = rangeWeights;
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                newRangeWeights[hero][hand] *= strategies[action][hand];
            }

            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            std::vector<float> actionExpectedValues = traverseTree(nextNode, constants, rules, newRangeWeights, tree);
            assert(actionExpectedValues.size() == heroRangeSize);

            for (int hand = 0; hand < heroRangeSize; ++hand) {
                expectedValues[hand] += actionExpectedValues[hand] * strategies[action][hand];

                // Regret update part 1 - add EV of action
                float& regretSum = tree.allRegretSums[getTrainingDataIndex(action, hand, rules, decisionNode, tree)];
                regretSum += villainReachSum * actionExpectedValues[hand];
            }
        }

        // Regret update part 2 - subtract total EV
        // Strategy update
        for (int action = 0; action < numActions; ++action) {
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                std::size_t index = getTrainingDataIndex(action, hand, rules, decisionNode, tree);
                float& regretSum = tree.allRegretSums[index];
                float& strategySum = tree.allStrategySums[index];

                regretSum -= villainReachSum * expectedValues[hand];
                strategySum += strategies[action][hand] * rangeWeights[hero][hand];

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
        &rangeWeights,
        &tree
    ]() -> std::vector<float> {
        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        Player hero = constants.hero;
        Player villain = getOpposingPlayer(hero);

        std::vector<std::vector<float>> strategies = getAverageStrategy(rules, decisionNode, tree);

        int heroRangeSize = rangeWeights[hero].size();
        std::vector<float> expectedValues(heroRangeSize, 0.0f);
        assert(heroRangeSize == strategies[0].size());

        for (int action = 0; action < numActions; ++action) {
            PlayerArray<std::vector<float>> newRangeWeights = rangeWeights;
            for (int hand = 0; hand < heroRangeSize; ++hand) {
                newRangeWeights[hero][hand] *= strategies[action][hand];
            }

            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            std::vector<float> actionExpectedValues = traverseTree(nextNode, constants, rules, newRangeWeights, tree);
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
        &rangeWeights,
        &tree
    ]() -> std::vector<float> {
        // To calculate best response, hero plays the maximally exploitative pure strategy
        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        Player hero = constants.hero;

        static constexpr float Infinity = std::numeric_limits<float>::infinity();
        int heroRangeSize = rangeWeights[hero].size();
        std::vector<float> expectedValues(heroRangeSize, -INFINITY);

        for (int action = 0; action < numActions; ++action) {
            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            std::vector<float> actionExpectedValues = traverseTree(nextNode, constants, rules, rangeWeights, tree);
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
        &rangeWeights,
        &tree
    ]() -> std::vector<float> {
        int numActions = static_cast<int>(decisionNode.decisionDataSize);
        assert(numActions > 0);

        Player hero = constants.hero;
        Player villain = getOpposingPlayer(hero);

        std::vector<std::vector<float>> strategies = getCurrentStrategy(rules, decisionNode, tree);

        int heroRangeSize = rangeWeights[hero].size();
        std::vector<float> expectedValues(heroRangeSize, 0.0f);

        int villainRangeSize = rangeWeights[villain].size();
        assert(villainRangeSize == strategies[0].size());

        // Not the hero's turn; no strategy or regret updates
        for (int action = 0; action < numActions; ++action) {
            PlayerArray<std::vector<float>> newRangeWeights = rangeWeights;
            for (int hand = 0; hand < villainRangeSize; ++hand) {
                newRangeWeights[villain][hand] *= strategies[action][hand];
            }

            std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
            assert(nextNodeIndex < tree.allNodes.size());
            const Node& nextNode = tree.allNodes[nextNodeIndex];

            std::vector<float> actionExpectedValues = traverseTree(nextNode, constants, rules, newRangeWeights, tree);
            assert(actionExpectedValues.size() == heroRangeSize);
            for (int i = 0; i < heroRangeSize; ++i) {
                float villianPossibleHandSum = 0.0f;
                for (int j = 0; j < villainRangeSize; ++j) {
                    if (areHandsDisjoint(i, j, constants, rules)) {
                        villianPossibleHandSum += rangeWeights[villain][j];
                    }
                }
                assert(villianPossibleHandSum >= 0.0f);

                if (villianPossibleHandSum > 0.0f) {
                    for (int j = 0; j < villainRangeSize; ++j) {
                        if (areHandsDisjoint(i, j, constants, rules)) {
                            // TODO: Don't recalculate villainHandProbability for each action
                            float villainHandProbability = rangeWeights[villain][j] / villianPossibleHandSum;
                            float villainActionProbability = villainHandProbability * strategies[action][j];
                            expectedValues[i] += actionExpectedValues[i] * villainActionProbability;
                        }
                    }
                }
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

std::vector<float> traverseFold(const FoldNode& foldNode, const TraversalConstants& constants, const IGameRules& rules) {
    // The expected value of a fold depends only on the size of the pot
    float reward = foldNode.remainingPlayerReward;
    float payoff = (constants.hero == foldNode.remainingPlayer) ? static_cast<float>(reward) : -static_cast<float>(reward);
    int heroRangeSize = rules.getInitialRangeWeights(constants.hero).size();
    return std::vector<float>(heroRangeSize, payoff);
}

std::vector<float> traverseShowdown(
    const ShowdownNode& showdownNode,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::vector<float>>& rangeWeights
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

    const std::vector<float>& heroWeights = rangeWeights[constants.hero];
    const std::vector<float>& villainWeights = rangeWeights[getOpposingPlayer(constants.hero)];

    int heroRangeSize = heroWeights.size();
    int villainRangeSize = villainWeights.size();

    assert(heroRangeSize == rules.getInitialRangeWeights(constants.hero).size());
    assert(villainRangeSize == rules.getInitialRangeWeights(getOpposingPlayer(constants.hero)).size());

    std::vector<float> expectedValues(heroRangeSize, 0.0f);

    for (int i = 0; i < heroRangeSize; ++i) {
        float villainPossibleHandSum = 0.0f;
        for (int j = 0; j < villainRangeSize; ++j) {
            if (areHandsDisjoint(i, j, constants, rules)) {
                assert(villainWeights[j] >= 0.0f);
                villainPossibleHandSum += villainWeights[j];
            }
        }

        assert(villainPossibleHandSum >= 0.0f);
        if (villainPossibleHandSum > 0.0f) {
            for (int j = 0; j < villainRangeSize; ++j) {
                if (areHandsDisjoint(i, j, constants, rules)) {
                    float matchupProbability = villainWeights[j] / villainPossibleHandSum;
                    expectedValues[i] += static_cast<float>(getMultiplier(i, j)) * showdownNode.reward * matchupProbability;
                }
            }
        }
    }

    return expectedValues;
}

std::vector<float> traverseTree(
    const Node& node,
    const TraversalConstants& constants,
    const IGameRules& rules,
    const PlayerArray<std::vector<float>>& rangeWeights,
    Tree& tree
) {
    assert(tree.isTreeSkeletonBuilt() && tree.isFullTreeBuilt());

    switch (node.getNodeType()) {
        case NodeType::Chance:
            return traverseChance(node.chanceNode, constants, rules, rangeWeights, tree);
        case NodeType::Decision:
            return traverseDecision(node.decisionNode, constants, rules, rangeWeights, tree);
        case NodeType::Fold:
            return traverseFold(node.foldNode, constants, rules);
        case NodeType::Showdown:
            return traverseShowdown(node.showdownNode, constants, rules, rangeWeights);
        default:
            assert(false);
            return {};
    }
}

std::vector<float> traverseFromRoot(const TraversalConstants& constants, const IGameRules& rules, Tree& tree) {
    std::size_t rootNodeIndex = tree.getRootNodeIndex();
    PlayerArray<std::vector<float>> rangeWeights = { rules.getInitialRangeWeights(Player::P0), rules.getInitialRangeWeights(Player::P1) };
    return traverseTree(
        tree.allNodes[rootNodeIndex],
        constants,
        rules,
        rangeWeights,
        tree
    );
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
    TraversalConstants constants = {
        .hero = hero,
        .mode = TraversalMode::ExpectedValue,
        .params = {} // No params needed for expected value
    };

    std::vector<float> expectedValueRange = traverseFromRoot(constants, rules, tree);
    const auto& heroRangeWeights = rules.getInitialRangeWeights(hero);
    assert(expectedValueRange.size() == heroRangeWeights.size());

    float expectedValue = 0.0f;
    for (int i = 0; i < expectedValueRange.size(); ++i) {
        expectedValue += expectedValueRange[i] * heroRangeWeights[i];
    }
    return expectedValue;
}

float bestResponseEV(
    Player hero,
    const IGameRules& rules,
    Tree& tree
) {
    TraversalConstants constants = {
        .hero = hero,
        .mode = TraversalMode::BestResponse,
        .params = {} // No params needed for best response
    };

    std::vector<float> bestResponse = traverseFromRoot(constants, rules, tree);
    const auto& heroRangeWeights = rules.getInitialRangeWeights(hero);
    assert(bestResponse.size() == heroRangeWeights.size());

    float bestResponseEV = 0.0f;
    for (int i = 0; i < bestResponse.size(); ++i) {
        bestResponseEV += bestResponse[i] * heroRangeWeights[i];
    }
    return bestResponseEV;
}

std::vector<std::vector<float>> getAverageStrategy(const IGameRules& rules, const DecisionNode& decisionNode, const Tree& tree) {
    int playerToActRangeSize = rules.getInitialRangeWeights(decisionNode.player).size();
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