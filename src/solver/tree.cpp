#include "solver/tree.hpp"

#include "game/game_types.hpp"
#include "game/game_rules.hpp"
#include "game/game_utils.hpp"
#include "util/fixed_vector.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
// sameHandIndexTable[p][i] = j iff the ith entry in player p's range is equal to the jth entry in the other player's range
// (or -1 if no such index exists)
// Used to calculate showdown and fold equity
PlayerArray<std::vector<int>> buildSameHandIndexTable(const IGameRules& rules) {
    const auto& player0Hands = rules.getRangeHands(Player::P0);
    const auto& player1Hands = rules.getRangeHands(Player::P1);

    int player0RangeSize = player0Hands.size();
    int player1RangeSize = player1Hands.size();

    PlayerArray<std::vector<int>> sameHandIndexTable = {
        std::vector<int>(player0RangeSize, -1),
        std::vector<int>(player1RangeSize, -1)
    };

    for (int i = 0; i < player0RangeSize; ++i) {
        for (int j = i; j < player1RangeSize; ++j) {
            if (player0Hands[i] == player1Hands[j]) {
                sameHandIndexTable[Player::P0][i] = j;
                sameHandIndexTable[Player::P1][j] = i;
            }
        }
    }

    return sameHandIndexTable;
}

double getTotalRangeWeight(const IGameRules& rules) {
    const auto& player0RangeWeights = rules.getInitialRangeWeights(Player::P0);
    const auto& player1RangeWeights = rules.getInitialRangeWeights(Player::P1);

    const auto& player0Hands = rules.getRangeHands(Player::P0);
    const auto& player1Hands = rules.getRangeHands(Player::P1);

    int player0RangeSize = player0Hands.size();
    int player1RangeSize = player1Hands.size();

    CardSet startingBoard = rules.getInitialGameState().currentBoard;

    double totalRangeWeight = 0.0;

    for (int i = 0; i < player0RangeSize; ++i) {
        if (!areSetsDisjoint(player0Hands[i], startingBoard)) continue;

        for (int j = 0; j < player1RangeSize; ++j) {
            if (!areSetsDisjoint(player0Hands[i] | startingBoard, player1Hands[j])) continue;

            totalRangeWeight += static_cast<double>(player0RangeWeights[i]) * static_cast<double>(player1RangeWeights[j]);
        }
    }

    return totalRangeWeight;
}

int getLastBetSize(const GameState& state) {
    int lastBetTotal = std::max(state.totalWagers[Player::P0], state.totalWagers[Player::P1]);
    return lastBetTotal - state.previousStreetsWager;
}
} // namespace

Tree::Tree() :
    rangeSize{ 0, 0 },
    gameHandSize{ 0 },
    deadMoney{ 0 },
    totalRangeWeight{ 0.0 },
    m_trainingDataSize{ 0 },
    m_numDecisionNodes{ 0 } {
}

bool Tree::isTreeSkeletonBuilt() const {
    return !allNodes.empty();
}

bool Tree::isFullTreeBuilt() const {
    return !allStrategySums.empty();
}

void Tree::buildTreeSkeleton(const IGameRules& rules) {
    std::size_t root = createNode(rules, rules.getInitialGameState());
    assert(root == allNodes.size() - 1);

    rangeHands = {
        rules.getRangeHands(Player::P0),
        rules.getRangeHands(Player::P1)
    };

    rangeSize = {
        static_cast<int>(rangeHands[Player::P0].size()),
        static_cast<int>(rangeHands[Player::P1].size()),
    };

    sameHandIndexTable = buildSameHandIndexTable(rules);

    // For now only games with 1 or 2 card hands are supported
    assert(!rangeHands[Player::P0].empty());
    assert(!rangeHands[Player::P1].empty());
    gameHandSize = getSetSize(rangeHands[Player::P0][0]);
    assert(gameHandSize == 1 || gameHandSize == 2);

    deadMoney = rules.getDeadMoney();

    // Range weight of 0 means that there are no valid combos of hands
    totalRangeWeight = getTotalRangeWeight(rules);
    assert(totalRangeWeight > 0.0);

    // Free unnecessary memory - vectors are done growing
    allNodes.shrink_to_fit();
    allChanceCards.shrink_to_fit();
    allChanceNextNodeIndices.shrink_to_fit();
    allDecisions.shrink_to_fit();
    allDecisionNextNodeIndices.shrink_to_fit();
    allDecisionBetRaiseSizes.shrink_to_fit();
}

std::size_t Tree::getNumberOfDecisionNodes() const {
    assert(isTreeSkeletonBuilt());
    return m_numDecisionNodes;
}

std::size_t Tree::getTreeSkeletonSize() const {
    assert(isTreeSkeletonBuilt());

    std::size_t treeStackSize = sizeof(Tree);
    std::size_t nodesHeapSize = allNodes.capacity() * sizeof(Node);
    std::size_t chanceHeapSize = (allChanceCards.capacity() * sizeof(CardID))
        + (allChanceNextNodeIndices.capacity() * sizeof(std::size_t));
    std::size_t decisionHeapSize = (allDecisions.capacity() * sizeof(ActionID))
        + (allDecisionNextNodeIndices.capacity() * sizeof(std::size_t))
        + (allDecisionBetRaiseSizes.capacity() * sizeof(int));
    std::size_t sameHandIndexTableSize = (sameHandIndexTable[Player::P0].capacity() + sameHandIndexTable[Player::P1].capacity()) * sizeof(int);
    return treeStackSize + nodesHeapSize + chanceHeapSize + decisionHeapSize + sameHandIndexTableSize;
}

std::size_t Tree::estimateFullTreeSize() const {
    assert(isTreeSkeletonBuilt());

    // allStrategySums and allRegretSums each have m_trainingDataLength elements
    std::size_t trainingDataHeapSize = (m_trainingDataSize * 2) * sizeof(float);

    return getTreeSkeletonSize() + trainingDataHeapSize;
}

std::size_t Tree::createNode(const IGameRules& rules, const GameState& state) {
    switch (rules.getNodeType(state)) {
        case NodeType::Chance:
            return createChanceNode(rules, state);
        case NodeType::Decision:
            return createDecisionNode(rules, state);
        case NodeType::Fold:
            return createFoldNode(state);
        case NodeType::Showdown:
            return createShowdownNode(state);
        default:
            assert(false);
            return 0;
    }
}

std::size_t Tree::createChanceNode(const IGameRules& rules, const GameState& state) {
    auto getParentSuit = [](Suit suit, const FixedVector<SuitEquivalenceClass, 4>& isomorphisms) -> Suit {
        for (SuitEquivalenceClass isomorphism : isomorphisms) {
            if (isomorphism.contains(suit)) {
                // Choose the first node to be the representative for this equivalence class
                return isomorphism[0];
            }
        }

        // Assume that nodes not present in the list are their own equivalence class
        return suit;
    };

    // Recurse to child nodes
    FixedVector<CardID, MaxNumDealCards> nextCards;
    FixedVector<std::size_t, MaxNumDealCards> nextNodeIndices;
    FixedVector<SuitMapping, 3> suitMappings;

    ChanceNodeInfo chanceNodeInfo = rules.getChanceNodeInfo(state.currentBoard);
    int numChanceCards = getSetSize(chanceNodeInfo.availableCards);

    CardSet temp = chanceNodeInfo.availableCards;
    for (int i = 0; i < numChanceCards; ++i) {
        CardID nextCard = popLowestCardFromSet(temp);

        Suit suit = getCardSuit(nextCard);
        Suit parentSuit = getParentSuit(suit, chanceNodeInfo.isomorphisms);

        if (suit == parentSuit) {
            // At a chance node both players should have wagered same amount
            assert(state.totalWagers[Player::P0] == state.totalWagers[Player::P1]);

            ActionID streetStart = rules.getInitialGameState().lastAction;

            GameState nextState = {
                .currentBoard = state.currentBoard | cardIDToSet(nextCard), // Add next card to board
                .totalWagers = state.totalWagers,
                .previousStreetsWager = state.totalWagers[Player::P0],
                .playerToAct = Player::P0, // Player 0 always starts a new betting round
                .lastAction = streetStart,
                .currentStreet = getNextStreet(state.currentStreet), // Advance to the next street after a chance node
            };

            nextCards.pushBack(nextCard);
            nextNodeIndices.pushBack(createNode(rules, nextState));
        }
        else {
            // This card would be equivalent to a card with the same value and the parent suit
            // We can save space by not storing it

            SuitMapping mapping = { .child = suit, .parent = parentSuit };
            if (!suitMappings.contains(mapping)) {
                suitMappings.pushBack(mapping);
            }
        }
    }
    assert(temp == 0);
    assert(nextCards.size() == nextNodeIndices.size());

    // Fill in current node information
    ChanceNode chanceNode = {
        .availableCards = chanceNodeInfo.availableCards,
        .chanceDataOffset = allChanceCards.size(),
        .suitMappings = suitMappings,
        .chanceDataSize = static_cast<std::uint8_t>(nextCards.size())
    };

    // Update tree information
    allChanceCards.insert(allChanceCards.end(), nextCards.begin(), nextCards.end());
    allChanceNextNodeIndices.insert(allChanceNextNodeIndices.end(), nextNodeIndices.begin(), nextNodeIndices.end());
    assert(allChanceCards.size() == allChanceNextNodeIndices.size());

    allNodes.emplace_back(chanceNode);
    return allNodes.size() - 1;
}

std::size_t Tree::createDecisionNode(const IGameRules& rules, const GameState& state) {
    // Recurse to child nodes
    FixedVector<ActionID, MaxNumActions> validActions = rules.getValidActions(state);
    FixedVector<std::size_t, MaxNumActions> nextNodeIndices;
    FixedVector<int, MaxNumActions> betRaiseSizes;
    for (ActionID actionID : validActions) {
        GameState newState = rules.getNewStateAfterDecision(state, actionID);
        nextNodeIndices.pushBack(createNode(rules, newState));
        betRaiseSizes.pushBack(getLastBetSize(newState));
    }
    assert(nextNodeIndices.size() == validActions.size());

    // Fill in current node information
    DecisionNode decisionNode = {
        .trainingDataOffset = m_trainingDataSize,
        .decisionDataOffset = allDecisions.size(),
        .decisionDataSize = static_cast<std::uint8_t>(validActions.size()),
        .playerToAct = state.playerToAct,
        .street = state.currentStreet
    };

    // Update tree information
    int playerToActRangeSize = rules.getInitialRangeWeights(state.playerToAct).size();
    std::size_t nodeTrainingDataSize = static_cast<std::size_t>(playerToActRangeSize) * decisionNode.decisionDataSize;
    m_trainingDataSize += nodeTrainingDataSize;
    allDecisions.insert(allDecisions.end(), validActions.begin(), validActions.end());
    allDecisionNextNodeIndices.insert(allDecisionNextNodeIndices.end(), nextNodeIndices.begin(), nextNodeIndices.end());
    allDecisionBetRaiseSizes.insert(allDecisionBetRaiseSizes.end(), betRaiseSizes.begin(), betRaiseSizes.end());
    assert(allDecisions.size() == allDecisionNextNodeIndices.size());
    assert(allDecisions.size() == allDecisionBetRaiseSizes.size());
    ++m_numDecisionNodes;

    allNodes.emplace_back(decisionNode);
    return allNodes.size() - 1;
}

std::size_t Tree::createFoldNode(const GameState& state) {
    // The folding player acted last turn
    Player foldingPlayer = getOpposingPlayer(state.playerToAct);

    FoldNode foldNode = {
        .board = state.currentBoard,
        .foldingPlayerWager = state.totalWagers[foldingPlayer],
        .foldingPlayer = foldingPlayer
    };

    allNodes.emplace_back(foldNode);
    return allNodes.size() - 1;
}

std::size_t Tree::createShowdownNode(const GameState& state) {
    // At showdown players should have wagered same amount
    assert(state.totalWagers[Player::P0] == state.totalWagers[Player::P1]);

    // Showdowns can only happen on the river
    assert(state.currentStreet == Street::River);

    ShowdownNode showdownNode = {
        .board = state.currentBoard,
        .playerWagers = state.totalWagers[Player::P0],
    };

    allNodes.emplace_back(showdownNode);
    return allNodes.size() - 1;
}

void Tree::buildFullTree() {
    assert(isTreeSkeletonBuilt() && !isFullTreeBuilt());

    allStrategySums.assign(m_trainingDataSize, 0.0f);
    allRegretSums.assign(m_trainingDataSize, 0.0f);
}

std::size_t Tree::getRootNodeIndex() const {
    assert(isTreeSkeletonBuilt() && isFullTreeBuilt());
    return allNodes.size() - 1;
}