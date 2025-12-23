#include <gtest/gtest.h>

#include "game/game_types.hpp"
#include "game/kuhn_poker.hpp"
#include "game/leduc_poker.hpp"
#include "solver/cfr.hpp"
#include "solver/tree.hpp"
#include "trainer/output.hpp"
#include "util/stack_allocator.hpp"

#ifdef _OPENMP
#include <omp.h>
#endif

TEST(EndToEndTest, Kuhn) {
    KuhnPoker kuhnPokerRules;
    Tree tree;
    tree.buildTreeSkeleton(kuhnPokerRules);

    // Test tree is correct structure
    ASSERT_EQ(tree.allNodes.size(), 9);
    ASSERT_EQ(tree.getNumberOfDecisionNodes(), 4);

    tree.initCfrVectors();

    StackAllocator<float> allocator(1);

    static constexpr float Iterations = 100000;
    for (int i = 0; i < Iterations; ++i) {
        for (Player hero : { Player::P0, Player::P1 }) {
            discountedCfr(hero, kuhnPokerRules, getDiscountParams(1.5f, 0.0f, 2.0f, i), tree, allocator);
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
    static constexpr float ExpectedPlayer0ExpectedValue = -1.0f / 18.0f;
    float player0ExpectedValue = expectedValue(Player::P0, kuhnPokerRules, tree, allocator);
    float player1ExpectedValue = expectedValue(Player::P1, kuhnPokerRules, tree, allocator);
    EXPECT_NEAR(player0ExpectedValue, ExpectedPlayer0ExpectedValue, StrategyEpsilon);
    EXPECT_NEAR(player1ExpectedValue, -ExpectedPlayer0ExpectedValue, StrategyEpsilon);

    // Make sure exploitability is non-negative and small
    static constexpr float ExploitabilityEpsilon = 1e-2;
    float exploitability = calculateExploitability(kuhnPokerRules, tree, allocator);
    ASSERT_GE(exploitability, 0.0f);
    ASSERT_NEAR(exploitability, 0.0f, ExploitabilityEpsilon);

    auto getNextDecisionNode = [&tree](const DecisionNode& decisionNode, KuhnActionID action) -> DecisionNode {
        std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + action];
        const Node& nextNode = tree.allNodes[nextNodeIndex];
        EXPECT_EQ(nextNode.getNodeType(), NodeType::Decision);

        return nextNode.decisionNode;
    };

    // Root node, player 0 to act
    // The first player is free to choose a probability 0 <= alpha <= 1/3 that they will bet with a Jack
    const Node& root = tree.allNodes[tree.getRootNodeIndex()];
    ASSERT_EQ(root.getNodeType(), NodeType::Decision);
    float alpha = getAverageStrategy(KuhnHandID::Jack, root.decisionNode, tree)[KuhnActionID::BetOrCall];
    ASSERT_GE(alpha, 0.0f);
    ASSERT_LE(alpha, 1.0f / 3.0f);
    ASSERT_NEAR(getAverageStrategy(KuhnHandID::Queen, root.decisionNode, tree)[KuhnActionID::BetOrCall], 0.0f, StrategyEpsilon);
    ASSERT_NEAR(getAverageStrategy(KuhnHandID::King, root.decisionNode, tree)[KuhnActionID::BetOrCall], 3.0f * alpha, StrategyEpsilon);

    // Check, player 1 to act
    DecisionNode check = getNextDecisionNode(root.decisionNode, KuhnActionID::CheckOrFold);
    ASSERT_NEAR(getAverageStrategy(KuhnHandID::Jack, check, tree)[KuhnActionID::BetOrCall], 1.0f / 3.0f, StrategyEpsilon);
    ASSERT_NEAR(getAverageStrategy(KuhnHandID::Queen, check, tree)[KuhnActionID::BetOrCall], 0.0f, StrategyEpsilon);
    ASSERT_NEAR(getAverageStrategy(KuhnHandID::King, check, tree)[KuhnActionID::BetOrCall], 1.0f, StrategyEpsilon);

    // Check Bet, player 0 to act
    DecisionNode checkBet = getNextDecisionNode(check, KuhnActionID::BetOrCall);
    ASSERT_NEAR(getAverageStrategy(KuhnHandID::Jack, checkBet, tree)[KuhnActionID::BetOrCall], 0.0f, StrategyEpsilon);
    ASSERT_NEAR(getAverageStrategy(KuhnHandID::Queen, checkBet, tree)[KuhnActionID::BetOrCall], alpha + (1.0f / 3.0f), StrategyEpsilon);
    ASSERT_NEAR(getAverageStrategy(KuhnHandID::King, checkBet, tree)[KuhnActionID::BetOrCall], 1.0f, StrategyEpsilon);

    // Bet, player 1 to act
    DecisionNode bet = getNextDecisionNode(root.decisionNode, KuhnActionID::BetOrCall);
    ASSERT_NEAR(getAverageStrategy(KuhnHandID::Jack, bet, tree)[KuhnActionID::BetOrCall], 0.0f, StrategyEpsilon);
    ASSERT_NEAR(getAverageStrategy(KuhnHandID::Queen, bet, tree)[KuhnActionID::BetOrCall], 1.0f / 3.0f, StrategyEpsilon);
    ASSERT_NEAR(getAverageStrategy(KuhnHandID::King, bet, tree)[KuhnActionID::BetOrCall], 1.0f, StrategyEpsilon);
}

TEST(EndToEndTest, LeducWithoutIsomorphism) {
    LeducPoker leducPokerRules(false);
    Tree tree;
    tree.buildTreeSkeleton(leducPokerRules);

    // Test tree is correct structure
    ASSERT_EQ(tree.allNodes.size(), 465);
    ASSERT_EQ(tree.getNumberOfDecisionNodes(), 186);

    tree.initCfrVectors();

    StackAllocator<float> allocator(1);

    static constexpr float Iterations = 10000;
    for (int i = 0; i < Iterations; ++i) {
        for (Player hero : { Player::P0, Player::P1 }) {
            discountedCfr(hero, leducPokerRules, getDiscountParams(1.5f, 0.0f, 2.0f, i + 1), tree, allocator);
        }
    }

    // https://cs.stackexchange.com/questions/169593/nash-equilibrium-details-for-leduc-holdem
    static constexpr float ExpectedPlayer0ExpectedValue = -0.0856;

    // Make sure EV is correct
    static constexpr float StrategyEpsilon = 1e-3;
    float player0ExpectedValue = expectedValue(Player::P0, leducPokerRules, tree, allocator);
    float player1ExpectedValue = expectedValue(Player::P1, leducPokerRules, tree, allocator);
    EXPECT_NEAR(player0ExpectedValue, ExpectedPlayer0ExpectedValue, StrategyEpsilon);
    EXPECT_NEAR(player1ExpectedValue, -ExpectedPlayer0ExpectedValue, StrategyEpsilon);

    // Make sure exploitability is non-negative and small
    static constexpr float ExploitabilityEpsilon = 1e-2;
    float exploitability = calculateExploitability(leducPokerRules, tree, allocator);
    ASSERT_GE(exploitability, 0.0f);
    ASSERT_NEAR(exploitability, 0.0f, ExploitabilityEpsilon);
}

TEST(EndToEndTest, LeducWithIsomorphism) {
    LeducPoker leducPokerRules(true);
    Tree tree;
    tree.buildTreeSkeleton(leducPokerRules);

    // Test tree is correct structure
    ASSERT_EQ(tree.allNodes.size(), 240);
    ASSERT_EQ(tree.getNumberOfDecisionNodes(), 96);

    tree.initCfrVectors();

    StackAllocator<float> allocator(1);

    static constexpr float Iterations = 10000;
    for (int i = 0; i < Iterations; ++i) {
        for (Player hero : { Player::P0, Player::P1 }) {
            discountedCfr(hero, leducPokerRules, getDiscountParams(1.5f, 0.0f, 2.0f, i + 1), tree, allocator);
        }
    }

    // https://cs.stackexchange.com/questions/169593/nash-equilibrium-details-for-leduc-holdem
    static constexpr float ExpectedPlayer0ExpectedValue = -0.0856;

    // Make sure EV is correct
    static constexpr float StrategyEpsilon = 1e-3;
    float player0ExpectedValue = expectedValue(Player::P0, leducPokerRules, tree, allocator);
    float player1ExpectedValue = expectedValue(Player::P1, leducPokerRules, tree, allocator);
    EXPECT_NEAR(player0ExpectedValue, ExpectedPlayer0ExpectedValue, StrategyEpsilon);
    EXPECT_NEAR(player1ExpectedValue, -ExpectedPlayer0ExpectedValue, StrategyEpsilon);

    // Make sure exploitability is non-negative and small
    static constexpr float ExploitabilityEpsilon = 1e-2;
    float exploitability = calculateExploitability(leducPokerRules, tree, allocator);
    ASSERT_GE(exploitability, 0.0f);
    ASSERT_NEAR(exploitability, 0.0f, ExploitabilityEpsilon);
}

TEST(EndToEndTest, LeducWithIsomorphismParallel) {
    #ifdef _OPENMP
    LeducPoker leducPokerRules(true);
    Tree tree;
    tree.buildTreeSkeleton(leducPokerRules);

    // Test tree is correct structure
    ASSERT_EQ(tree.allNodes.size(), 240);
    ASSERT_EQ(tree.getNumberOfDecisionNodes(), 96);

    tree.initCfrVectors();

    static constexpr int NumThreads = 6;
    StackAllocator<float> allocator(NumThreads);
    omp_set_num_threads(NumThreads);

    #pragma omp parallel
    {
        #pragma omp single
        {
            static constexpr float Iterations = 10000;
            for (int i = 0; i < Iterations; ++i) {
                for (Player hero : { Player::P0, Player::P1 }) {
                    discountedCfr(hero, leducPokerRules, getDiscountParams(1.5f, 0.0f, 2.0f, i + 1), tree, allocator);
                }
            }
        }
    }

    // https://cs.stackexchange.com/questions/169593/nash-equilibrium-details-for-leduc-holdem
    static constexpr float ExpectedPlayer0ExpectedValue = -0.0856;

    // Make sure EV is correct
    static constexpr float StrategyEpsilon = 1e-3;
    float player0ExpectedValue = expectedValue(Player::P0, leducPokerRules, tree, allocator);
    float player1ExpectedValue = expectedValue(Player::P1, leducPokerRules, tree, allocator);
    EXPECT_NEAR(player0ExpectedValue, ExpectedPlayer0ExpectedValue, StrategyEpsilon);
    EXPECT_NEAR(player1ExpectedValue, -ExpectedPlayer0ExpectedValue, StrategyEpsilon);

    // Make sure exploitability is non-negative and small
    static constexpr float ExploitabilityEpsilon = 1e-2;
    float exploitability = calculateExploitability(leducPokerRules, tree, allocator);
    ASSERT_GE(exploitability, 0.0f);
    ASSERT_NEAR(exploitability, 0.0f, ExploitabilityEpsilon);
    #else
    GTEST_SKIP() << "OMP not found, skipping parallel test.";
    #endif
}

TEST(EndToEndTest, LeducSerialAndParallelAreIdentical) {
    #ifdef _OPENMP
    struct LeducResult {
        float player0ExpectedValue;
        float player1ExpectedValue;
        float exploitability;

        bool operator==(const LeducResult&) const = default;
    };

    LeducResult serial, parallel;

    // Serial code
    {
        LeducPoker leducPokerRules(true);
        Tree tree;
        tree.buildTreeSkeleton(leducPokerRules);
        tree.initCfrVectors();

        StackAllocator<float> allocator(1);

        static constexpr float Iterations = 10000;
        for (int i = 0; i < Iterations; ++i) {
            for (Player hero : { Player::P0, Player::P1 }) {
                discountedCfr(hero, leducPokerRules, getDiscountParams(1.5f, 0.0f, 2.0f, i + 1), tree, allocator);
            }
        }

        serial = {
            .player0ExpectedValue = expectedValue(Player::P0, leducPokerRules, tree, allocator),
            .player1ExpectedValue = expectedValue(Player::P1, leducPokerRules, tree, allocator),
            .exploitability = calculateExploitability(leducPokerRules, tree, allocator)
        };
    }

    // Parallel code
    {
        LeducPoker leducPokerRules(true);
        Tree tree;
        tree.buildTreeSkeleton(leducPokerRules);
        tree.initCfrVectors();

        static constexpr int NumThreads = 6;
        StackAllocator<float> allocator(NumThreads);
        omp_set_num_threads(NumThreads);

        #pragma omp parallel
        {
            #pragma omp single
            {
                static constexpr float Iterations = 10000;
                for (int i = 0; i < Iterations; ++i) {
                    for (Player hero : { Player::P0, Player::P1 }) {
                        discountedCfr(hero, leducPokerRules, getDiscountParams(1.5f, 0.0f, 2.0f, i + 1), tree, allocator);
                    }
                }
            }
        }

        parallel = {
            .player0ExpectedValue = expectedValue(Player::P0, leducPokerRules, tree, allocator),
            .player1ExpectedValue = expectedValue(Player::P1, leducPokerRules, tree, allocator),
            .exploitability = calculateExploitability(leducPokerRules, tree, allocator)
        };
    }

    EXPECT_EQ(serial, parallel);
    #else
    GTEST_SKIP() << "OMP not found, skipping parallel test.";
    #endif
}