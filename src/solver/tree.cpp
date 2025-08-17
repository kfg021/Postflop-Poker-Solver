#include "solver/tree.hpp"

#include "game/game_types.hpp"
#include "game/game_rules.hpp"
#include "game/game_utils.hpp"
#include "util/fixed_vector.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

bool Tree::isTreeSkeletonBuilt() const {
    return !allNodes.empty();
}

bool Tree::isFullTreeBuilt() const {
    return !allStrategySums.empty();
}

void Tree::buildTreeSkeleton(const IGameRules& rules) {
    std::size_t root = createNode(rules, rules.getInitialGameState());
    assert(root == allNodes.size() - 1);

    // Free unnecessary memory - vectors are done growing
    allNodes.shrink_to_fit();
    allChanceCards.shrink_to_fit();
    allChanceNextNodeIndices.shrink_to_fit();
    allDecisions.shrink_to_fit();
    allDecisionNextNodeIndices.shrink_to_fit();
}

std::size_t Tree::getNumberOfDecisionNodes() const {
    assert(isTreeSkeletonBuilt());
    return m_numDecisionNodes;
}

std::size_t Tree::getTreeSkeletonSize() const {
    assert(isTreeSkeletonBuilt());

    std::size_t treeStackSize = sizeof(Tree);
    std::size_t nodesHeapSize = allNodes.capacity() * sizeof(Node);
    std::size_t chanceHeapSize = (allChanceCards.capacity() * sizeof(CardID)) + (allChanceNextNodeIndices.capacity() * sizeof(std::size_t));
    std::size_t decisionHeapSize = (allDecisions.capacity() * sizeof(ActionID)) + (allDecisionNextNodeIndices.capacity() * sizeof(std::size_t));
    return treeStackSize + nodesHeapSize + chanceHeapSize + decisionHeapSize;
}

std::size_t Tree::estimateFullTreeSize() const {
    assert(isTreeSkeletonBuilt());

    // allStrategySums and allRegretSums will each have length of trainingDataLength
    std::size_t trainingDataHeapSize = (m_trainingDataLength * 2) * sizeof(float);
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
    // There should only be one action and it should be a chance action
    FixedVector<ActionID, MaxNumActions> validActions = rules.getValidActions(state);
    assert(validActions.size() == 1 && rules.getActionType(validActions[0]) == ActionType::Chance);

    // Recurse to child nodes
    CardSet availableCards = rules.getDeck() & ~state.currentBoard;
    FixedVector<CardID, MaxNumDealCards> nextCards;
    FixedVector<std::size_t, MaxNumDealCards> nextNodeIndices;
    for (CardID cardID = 0; cardID < StandardDeckSize; ++cardID) {
        if (setContainsCard(availableCards, cardID)) {
            CardSet newBoard = state.currentBoard | cardIDToSet(cardID);

            GameState newState = {
                .currentBoard = newBoard,
                .totalWagers = state.totalWagers,
                .deadMoney = state.deadMoney,
                .playerToAct = Player::P0, // Player 0 always starts a new betting round
                .lastAction = validActions[0],
                .currentStreet = nextStreet(state.currentStreet), // After a card is dealt we move to the next street
            };

            nextCards.pushBack(cardID);
            nextNodeIndices.pushBack(createNode(rules, newState));
        }
    }
    assert(nextCards.size() == nextNodeIndices.size());

    // Fill in current node information
    ChanceNode chanceNode = {
        .board = state.currentBoard,
        .chanceDataOffset = allChanceCards.size(),
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
    for (ActionID actionID : validActions) {
        assert(rules.getActionType(actionID) == ActionType::Decision);

        GameState newState = rules.getNewStateAfterDecision(state, actionID);
        nextNodeIndices.pushBack(createNode(rules, newState));
    }
    assert(nextNodeIndices.size() == validActions.size());

    // Fill in current node information
    DecisionNode decisionNode = {
        .trainingDataOffset = m_trainingDataLength,
        .decisionDataOffset = allDecisions.size(),
        .numTrainingDataSets = rules.getRangeSize(state.playerToAct),
        .decisionDataSize = static_cast<std::uint8_t>(validActions.size()),
        .player = state.playerToAct
    };

    // Update tree information
    std::size_t nodeTrainingDataLength = static_cast<std::size_t>(decisionNode.numTrainingDataSets) * decisionNode.decisionDataSize;
    m_trainingDataLength += nodeTrainingDataLength;
    allDecisions.insert(allDecisions.end(), validActions.begin(), validActions.end());
    allDecisionNextNodeIndices.insert(allDecisionNextNodeIndices.end(), nextNodeIndices.begin(), nextNodeIndices.end());
    assert(allDecisions.size() == allDecisionNextNodeIndices.size());
    ++m_numDecisionNodes;

    allNodes.emplace_back(decisionNode);
    return allNodes.size() - 1;
}

std::size_t Tree::createFoldNode(const GameState& state) {
    // The reward is the amount that the folding player wagered plus any dead money
    // The folding player acted last turn
    int remaningPlayerReward = state.totalWagers[getOpposingPlayer(state.playerToAct)] + state.deadMoney;

    FoldNode foldNode = {
        .remainingPlayerReward = remaningPlayerReward,
        .remainingPlayer = state.playerToAct
    };

    allNodes.emplace_back(foldNode);
    return allNodes.size() - 1;
}

std::size_t Tree::createShowdownNode(const GameState& state) {
    // At showdown players should have wagered same amount
    assert(state.totalWagers[Player::P0] == state.totalWagers[Player::P1]);

    // The reward is the amount wagered plus any dead money
    int reward = state.totalWagers[Player::P0] + state.deadMoney;

    ShowdownNode showdownNode = {
        .board = state.currentBoard,
        .reward = reward,
        .street = state.currentStreet
    };

    allNodes.emplace_back(showdownNode);
    return allNodes.size() - 1;
}

void Tree::buildFullTree() {
    assert(isTreeSkeletonBuilt() && !isFullTreeBuilt());

    allStrategySums.assign(m_trainingDataLength, 0.0f);
    allStrategySums.shrink_to_fit();

    allRegretSums.assign(m_trainingDataLength, 0.0f);
    allRegretSums.shrink_to_fit();
}

Node Tree::getRootNode() const {
    assert(isTreeSkeletonBuilt() && isFullTreeBuilt());
    return allNodes.back();
}

std::size_t getTrainingDataIndex(const DecisionNode& decisionNode, std::uint16_t trainingDataSet, std::uint8_t actionIndex) {
    assert(trainingDataSet < decisionNode.numTrainingDataSets);
    return decisionNode.trainingDataOffset + (trainingDataSet * decisionNode.decisionDataSize) + actionIndex;
}

FixedVector<float, MaxNumActions> getAverageStrategy(const DecisionNode& decisionNode, const Tree& tree, std::uint16_t trainingDataSet) {
    std::uint8_t numActions = decisionNode.decisionDataSize;
    assert(numActions > 0);

    float total = 0.0f;
    for (int i = 0; i < numActions; ++i) {
        total += tree.allStrategySums[getTrainingDataIndex(decisionNode, trainingDataSet, i)];
    }

    if (total == 0.0f) {
        FixedVector<float, MaxNumActions> uniformStrategy(numActions, 1.0f / numActions);
        return uniformStrategy;
    }

    FixedVector<float, MaxNumActions> averageStrategy(numActions, 0.0f);
    for (int i = 0; i < numActions; ++i) {
        averageStrategy[i] = tree.allStrategySums[getTrainingDataIndex(decisionNode, trainingDataSet, i)] / total;
    }

    return averageStrategy;
}