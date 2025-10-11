#include <gtest/gtest.h>

#include "game/game_types.hpp"
#include "game/kuhn_poker.hpp"
#include "solver/cfr.hpp"
#include "solver/tree.hpp"
#include "trainer/output.hpp"

TEST(KuhnPokerTest, KuhnPokerE2ETest) {
    KuhnPoker kuhnPokerRules;
    Tree tree;
    tree.buildTreeSkeleton(kuhnPokerRules);

    // Test tree is correct structure
    ASSERT_EQ(tree.allNodes.size(), 9);
    ASSERT_EQ(tree.getNumberOfDecisionNodes(), 4);

    tree.buildFullTree();

    static constexpr float Iterations = 100000;
    for (int i = 0; i < Iterations; ++i) {
        for (Player hero : { Player::P0, Player::P1 }) {
            discountedCfr(hero, kuhnPokerRules, getDiscountParams(1.5f, 0.0f, 2.0f, i), tree);
        }
    }

    enum KuhnActionID : std::uint8_t {
        CheckOrFold,
        BetOrCall
    };

    enum KuhnHandID : std::uint8_t {
        Jack,
        Queen,
        King
    };

    static constexpr float StrategyEpsilon = 1e-3;

    // Test that strategy is optimal
    // https://en.wikipedia.org/wiki/Kuhn_poker#Optimal_strategy

    // Kuhn poker has a known EV of -1/18 for the starting player
    float player0ExpectedValue = expectedValue(Player::P0, kuhnPokerRules, tree);
    static constexpr float ExpectedPlayer0ExpectedValue = -1.0f / 18.0f;
    EXPECT_NEAR(player0ExpectedValue, ExpectedPlayer0ExpectedValue, StrategyEpsilon);

    // Make sure the exploitative strategies are at least as strong as the Nash strategy
    float player0BestResponseEV = bestResponseEV(Player::P0, kuhnPokerRules, tree);
    float player1BestResponseEV = bestResponseEV(Player::P1, kuhnPokerRules, tree);
    ASSERT_GE(player0BestResponseEV, player0ExpectedValue);
    ASSERT_GE(player1BestResponseEV, -player0ExpectedValue);

    // Make sure exploitability is non-negative and small
    static constexpr float ExploitabilityEpsilon = 1e-2;
    float exploitability = (player0BestResponseEV + player1BestResponseEV) / 2.0f;
    ASSERT_GE(exploitability, 0.0f);
    ASSERT_NEAR(exploitability, 0.0f, ExploitabilityEpsilon);

    auto getNextDecisionNode = [&tree](const DecisionNode& decisionNode, KuhnActionID action) -> DecisionNode {
        std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
        const Node& nextNode = tree.allNodes[nextNodeIndex];
        EXPECT_EQ(nextNode.getNodeType(), NodeType::Decision);

        return nextNode.decisionNode;
    };

    auto getStrategyValue = [&kuhnPokerRules, &tree](std::uint8_t action, std::uint8_t hand, const DecisionNode& decisionNode) -> float {
        std::size_t trainingIndex = getTrainingDataIndex(
            static_cast<int>(action),
            static_cast<int>(hand),
            kuhnPokerRules,
            decisionNode,
            tree
        );
        return tree.allStrategies[trainingIndex];
    };

    // Root node, player 0 to act
    // The first player is free to choose a probability 0 <= alpha <= 1/3 that they will bet with a Jack
    const Node& root = tree.allNodes[tree.getRootNodeIndex()];
    ASSERT_EQ(root.getNodeType(), NodeType::Decision);
    writeAverageStrategyToBuffer(kuhnPokerRules, root.decisionNode, tree);
    float alpha = getStrategyValue(KuhnActionID::BetOrCall, KuhnHandID::Jack, root.decisionNode);
    ASSERT_GE(alpha, 0.0f);
    ASSERT_LE(alpha, 1.0f / 3.0f);
    ASSERT_NEAR(getStrategyValue(KuhnActionID::BetOrCall, KuhnHandID::Queen, root.decisionNode), 0.0f, StrategyEpsilon);
    ASSERT_NEAR(getStrategyValue(KuhnActionID::BetOrCall, KuhnHandID::King, root.decisionNode), 3.0f * alpha, StrategyEpsilon);

    // Check, player 1 to act
    DecisionNode check = getNextDecisionNode(root.decisionNode, KuhnActionID::CheckOrFold);
    writeAverageStrategyToBuffer(kuhnPokerRules, check, tree);
    ASSERT_NEAR(getStrategyValue(KuhnActionID::BetOrCall, KuhnHandID::Jack, check), 1.0f / 3.0f, StrategyEpsilon);
    ASSERT_NEAR(getStrategyValue(KuhnActionID::BetOrCall, KuhnHandID::Queen, check), 0.0f, StrategyEpsilon);
    ASSERT_NEAR(getStrategyValue(KuhnActionID::BetOrCall, KuhnHandID::King, check), 1.0f, StrategyEpsilon);

    // Check Bet, player 0 to act
    DecisionNode checkBet = getNextDecisionNode(check, KuhnActionID::BetOrCall);
    writeAverageStrategyToBuffer(kuhnPokerRules, checkBet, tree);
    ASSERT_NEAR(getStrategyValue(KuhnActionID::BetOrCall, KuhnHandID::Jack, checkBet), 0.0f, StrategyEpsilon);
    ASSERT_NEAR(getStrategyValue(KuhnActionID::BetOrCall, KuhnHandID::Queen, checkBet), alpha + (1.0f / 3.0f), StrategyEpsilon);
    ASSERT_NEAR(getStrategyValue(KuhnActionID::BetOrCall, KuhnHandID::King, checkBet), 1.0f, StrategyEpsilon);

    // Bet, player 1 to act
    DecisionNode bet = getNextDecisionNode(root.decisionNode, KuhnActionID::BetOrCall);
    writeAverageStrategyToBuffer(kuhnPokerRules, bet, tree);
    ASSERT_NEAR(getStrategyValue(KuhnActionID::BetOrCall, KuhnHandID::Jack, bet), 0.0f, StrategyEpsilon);
    ASSERT_NEAR(getStrategyValue(KuhnActionID::BetOrCall, KuhnHandID::Queen, bet), 1.0f / 3.0f, StrategyEpsilon);
    ASSERT_NEAR(getStrategyValue(KuhnActionID::BetOrCall, KuhnHandID::King, bet), 1.0f, StrategyEpsilon);
}