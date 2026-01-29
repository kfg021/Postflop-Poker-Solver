#include <gtest/gtest.h>

#include "game/game_types.hpp"
#include "game/kuhn_poker.hpp"
#include "game/leduc_poker.hpp"
#include "solver/cfr.hpp"
#include "solver/tree.hpp"
#include "util/stack_allocator.hpp"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {
    static constexpr int KuhnIterations = 100000;
    static constexpr int LeducIterations = 10000;

    // https://en.wikipedia.org/wiki/Kuhn_poker#Optimal_strategy
    static constexpr float KuhnPlayer0ExpectedValue = -1.0f / 18.0f;

    // Lanctot, M., Zambaldi, V., Gruslys, A., Lazaridou, A., Tuyls, K., Perolat, J., Silver, D., & Graepel, T. (2017).
    // A Unified Game-Theoretic Approach to Multiagent Reinforcement Learning.
    // https://doi.org/10.48550/arXiv.1711.00832
    static constexpr float LeducPlayer0ExpectedValue = -0.0856f;

    static constexpr float StrategyEpsilon = 1e-3f;
    static constexpr float ExploitabilityEpsilon = 1e-2f;

    static constexpr int NumParallelThreads = 6;

    DiscountParams getTestingDiscountParams(int index) {
        return getDiscountParams(1.5f, 0.0f, 2.0f, index + 1);
    }
} // namespace

TEST(EndToEndTest, Kuhn) {
    KuhnPoker kuhnPokerRules;
    Tree tree;
    tree.buildTreeSkeleton(kuhnPokerRules);

    // Test tree is correct structure
    ASSERT_EQ(tree.allNodes.size(), 9);
    ASSERT_EQ(tree.getNumberOfDecisionNodes(), 4);

    tree.initCfrVectors();

    StackAllocator<float> allocator(1);

    for (int i = 0; i < KuhnIterations; ++i) {
        for (Player hero : { Player::P0, Player::P1 }) {
            discountedCfr(hero, kuhnPokerRules, getTestingDiscountParams(i), tree, allocator);
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

    // Test that strategy is optimal
    // https://en.wikipedia.org/wiki/Kuhn_poker#Optimal_strategy

    float player0ExpectedValue = expectedValue(Player::P0, kuhnPokerRules, tree, allocator);
    float player1ExpectedValue = expectedValue(Player::P1, kuhnPokerRules, tree, allocator);
    EXPECT_NEAR(player0ExpectedValue, KuhnPlayer0ExpectedValue, StrategyEpsilon);
    EXPECT_NEAR(player1ExpectedValue, -KuhnPlayer0ExpectedValue, StrategyEpsilon);

    // Make sure exploitability is non-negative and small
    float exploitability = calculateExploitability(kuhnPokerRules, tree, allocator);
    ASSERT_GE(exploitability, 0.0f);
    ASSERT_NEAR(exploitability, 0.0f, ExploitabilityEpsilon);

    // Root node, player 0 to act
    // The first player is free to choose a probability 0 <= alpha <= 1/3 that they will bet with a Jack
    const Node& root = tree.allNodes[tree.getRootNodeIndex()];
    ASSERT_EQ(root.nodeType, NodeType::Decision);
    float alpha = getFinalStrategy(KuhnHandID::Jack, root, tree)[KuhnActionID::BetOrCall];
    ASSERT_GE(alpha, 0.0f);
    ASSERT_LE(alpha, 1.0f / 3.0f);
    ASSERT_NEAR(getFinalStrategy(KuhnHandID::Queen, root, tree)[KuhnActionID::BetOrCall], 0.0f, StrategyEpsilon);
    ASSERT_NEAR(getFinalStrategy(KuhnHandID::King, root, tree)[KuhnActionID::BetOrCall], 3.0f * alpha, StrategyEpsilon);

    // Check, player 1 to act
    const Node& check = tree.allNodes[root.childrenOffset + KuhnActionID::CheckOrFold];
    ASSERT_EQ(check.nodeType, NodeType::Decision);
    ASSERT_NEAR(getFinalStrategy(KuhnHandID::Jack, check, tree)[KuhnActionID::BetOrCall], 1.0f / 3.0f, StrategyEpsilon);
    ASSERT_NEAR(getFinalStrategy(KuhnHandID::Queen, check, tree)[KuhnActionID::BetOrCall], 0.0f, StrategyEpsilon);
    ASSERT_NEAR(getFinalStrategy(KuhnHandID::King, check, tree)[KuhnActionID::BetOrCall], 1.0f, StrategyEpsilon);

    // Check Bet, player 0 to act
    const Node& checkBet = tree.allNodes[check.childrenOffset + KuhnActionID::BetOrCall];
    ASSERT_EQ(checkBet.nodeType, NodeType::Decision);
    ASSERT_NEAR(getFinalStrategy(KuhnHandID::Jack, checkBet, tree)[KuhnActionID::BetOrCall], 0.0f, StrategyEpsilon);
    ASSERT_NEAR(getFinalStrategy(KuhnHandID::Queen, checkBet, tree)[KuhnActionID::BetOrCall], alpha + (1.0f / 3.0f), StrategyEpsilon);
    ASSERT_NEAR(getFinalStrategy(KuhnHandID::King, checkBet, tree)[KuhnActionID::BetOrCall], 1.0f, StrategyEpsilon);

    // Bet, player 1 to act
    const Node& bet = tree.allNodes[root.childrenOffset + KuhnActionID::BetOrCall];
    ASSERT_NEAR(getFinalStrategy(KuhnHandID::Jack, bet, tree)[KuhnActionID::BetOrCall], 0.0f, StrategyEpsilon);
    ASSERT_NEAR(getFinalStrategy(KuhnHandID::Queen, bet, tree)[KuhnActionID::BetOrCall], 1.0f / 3.0f, StrategyEpsilon);
    ASSERT_NEAR(getFinalStrategy(KuhnHandID::King, bet, tree)[KuhnActionID::BetOrCall], 1.0f, StrategyEpsilon);
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

    for (int i = 0; i < LeducIterations; ++i) {
        for (Player hero : { Player::P0, Player::P1 }) {
            discountedCfr(hero, leducPokerRules, getTestingDiscountParams(i), tree, allocator);
        }
    }

    // Make sure EV is correct
    float player0ExpectedValue = expectedValue(Player::P0, leducPokerRules, tree, allocator);
    float player1ExpectedValue = expectedValue(Player::P1, leducPokerRules, tree, allocator);
    EXPECT_NEAR(player0ExpectedValue, LeducPlayer0ExpectedValue, StrategyEpsilon);
    EXPECT_NEAR(player1ExpectedValue, -LeducPlayer0ExpectedValue, StrategyEpsilon);

    // Make sure exploitability is non-negative and small
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

    for (int i = 0; i < LeducIterations; ++i) {
        for (Player hero : { Player::P0, Player::P1 }) {
            discountedCfr(hero, leducPokerRules, getTestingDiscountParams(i), tree, allocator);
        }
    }

    // Make sure EV is correct
    float player0ExpectedValue = expectedValue(Player::P0, leducPokerRules, tree, allocator);
    float player1ExpectedValue = expectedValue(Player::P1, leducPokerRules, tree, allocator);
    EXPECT_NEAR(player0ExpectedValue, LeducPlayer0ExpectedValue, StrategyEpsilon);
    EXPECT_NEAR(player1ExpectedValue, -LeducPlayer0ExpectedValue, StrategyEpsilon);

    // Make sure exploitability is non-negative and small
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

    StackAllocator<float> allocator(NumParallelThreads);
    omp_set_num_threads(NumParallelThreads);

    #pragma omp parallel
    {
        #pragma omp single
        {
            for (int i = 0; i < LeducIterations; ++i) {
                for (Player hero : { Player::P0, Player::P1 }) {
                    discountedCfr(hero, leducPokerRules, getTestingDiscountParams(i), tree, allocator);
                }
            }
        }
    }

    // Make sure EV is correct
    float player0ExpectedValue = expectedValue(Player::P0, leducPokerRules, tree, allocator);
    float player1ExpectedValue = expectedValue(Player::P1, leducPokerRules, tree, allocator);
    EXPECT_NEAR(player0ExpectedValue, LeducPlayer0ExpectedValue, StrategyEpsilon);
    EXPECT_NEAR(player1ExpectedValue, -LeducPlayer0ExpectedValue, StrategyEpsilon);

    // Make sure exploitability is non-negative and small
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

        for (int i = 0; i < LeducIterations; ++i) {
            for (Player hero : { Player::P0, Player::P1 }) {
                discountedCfr(hero, leducPokerRules, getTestingDiscountParams(i), tree, allocator);
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

        StackAllocator<float> allocator(NumParallelThreads);
        omp_set_num_threads(NumParallelThreads);

        #pragma omp parallel
        {
            #pragma omp single
            {
                for (int i = 0; i < LeducIterations; ++i) {
                    for (Player hero : { Player::P0, Player::P1 }) {
                        discountedCfr(hero, leducPokerRules, getTestingDiscountParams(i), tree, allocator);
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