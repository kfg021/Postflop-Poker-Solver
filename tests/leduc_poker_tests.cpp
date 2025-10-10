#include <gtest/gtest.h>

#include "game/game_types.hpp"
#include "game/leduc_poker.hpp"
#include "solver/cfr.hpp"
#include "solver/tree.hpp"
#include "trainer/output.hpp"

TEST(LeducPokerTest, LeducPokerE2ETest) {
    LeducPoker leducPokerRules;
    Tree tree;
    tree.buildTreeSkeleton(leducPokerRules);

    // Test tree is correct structure
    ASSERT_EQ(tree.allNodes.size(), 465);
    ASSERT_EQ(tree.getNumberOfDecisionNodes(), 186);

    tree.buildFullTree();

    static constexpr float Iterations = 10000;
    for (int i = 0; i < Iterations; ++i) {
        for (Player hero : { Player::P0, Player::P1 }) {
            discountedCfr(hero, leducPokerRules, getDiscountParams(1.5f, 0.0f, 2.0f, i), tree);
        }
    }

    // https://cs.stackexchange.com/questions/169593/nash-equilibrium-details-for-leduc-holdem
    static constexpr float ExpectedPlayer0ExpectedValue = -0.0856;
    
    // Make sure EV is correct
    static constexpr float StrategyEpsilon = 1e-3;
    float player0ExpectedValue = expectedValue(Player::P0, leducPokerRules, tree);
    EXPECT_NEAR(player0ExpectedValue, ExpectedPlayer0ExpectedValue, StrategyEpsilon);

   // Make sure the exploitative strategies are at least as strong as the Nash strategy
    float player0BestResponseEV = bestResponseEV(Player::P0, leducPokerRules, tree);
    float player1BestResponseEV = bestResponseEV(Player::P1, leducPokerRules, tree);
    ASSERT_GE(player0BestResponseEV, player0ExpectedValue);
    ASSERT_GE(player1BestResponseEV, -player0ExpectedValue);

    // Make sure exploitability is non-negative and small
    static constexpr float ExploitabilityEpsilon = 1e-2;
    float exploitability = (player0BestResponseEV + player1BestResponseEV) / 2.0f;
    ASSERT_GE(exploitability, 0.0f);
    ASSERT_NEAR(exploitability, 0.0f, ExploitabilityEpsilon);
}