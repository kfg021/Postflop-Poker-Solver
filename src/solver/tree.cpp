#include "solver/tree.hpp"

#include "game/game_types.hpp"
#include "game/game_rules.hpp"
#include "game/game_utils.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

bool Tree::isTreeSkeletonBuilt() const {
    return !allNodes.empty();
}

bool Tree::isFullTreeBuilt() const {
    return !allStrategySums.empty();
}

void Tree::buildTreeSkeleton(const IGameRules& rules, const std::array<std::uint16_t, 2>& rangeSizes) {
    std::vector<ActionID> actionHistory;
    std::unordered_map<std::vector<ActionID>, std::size_t, ActionHistoryHash> nodeIndexMap;
    std::size_t root = createNodeRecursive(rules, rules.getInitialGameState(), rangeSizes, actionHistory, nodeIndexMap);
    assert(root == allNodes.size() - 1);

    allNodes.shrink_to_fit();
    allActions.shrink_to_fit();
    allNextNodeIndices.shrink_to_fit();
}

std::size_t Tree::getTreeSkeletonSize() const {
    std::size_t treeStackSize = sizeof(Tree);
    std::size_t nodesHeapSize = allNodes.capacity() * sizeof(Node);
    std::size_t actionsHeapSize = allActions.capacity() * sizeof(ActionID);
    std::size_t childrenHeapSize = allNextNodeIndices.capacity() * sizeof(std::size_t);
    return treeStackSize + nodesHeapSize + actionsHeapSize + childrenHeapSize;
}

std::size_t Tree::estimateFullTreeSize() const {
    // allStrategySums and allRegretSums will each have length of trainingDataLength
    std::size_t trainingDataHeapSize = (trainingDataLength * 2) * sizeof(float);
    return getTreeSkeletonSize() + trainingDataHeapSize;
}

std::size_t Tree::ActionHistoryHash::operator()(const std::vector<ActionID>& actionHistory) const {
    // Hash by treating vector as a string view
    return std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char*>(actionHistory.data()), actionHistory.size()));
}

std::size_t Tree::createNodeRecursive(
    const IGameRules& rules,
    const GameState& state,
    const std::array<std::uint16_t, 2>& rangeSizes,
    std::vector<ActionID>& actionHistory,
    std::unordered_map<std::vector<ActionID>, std::size_t, ActionHistoryHash>& nodeIndexMap
) {
    if (auto it = nodeIndexMap.find(actionHistory); it != nodeIndexMap.end()) {
        return it->second;
    }

    std::size_t nodeIndex;
    switch (rules.getNodeType(state)) {
        case NodeType::Chance:
            nodeIndex = createChanceNodeRecursive(rules, state, rangeSizes, actionHistory, nodeIndexMap);
            break;
        case NodeType::Decision:
            nodeIndex = createDecisionNodeRecursive(rules, state, rangeSizes, actionHistory, nodeIndexMap);
            break;
        case NodeType::Fold:
            nodeIndex = createFoldNode(state);
            break;
        case NodeType::Showdown:
            nodeIndex = createShowdownNode(state);
            break;
        default:
            assert(false);
            nodeIndex = static_cast<std::size_t>(-1);
            break;
    }

    assert(nodeIndexMap.find(actionHistory) == nodeIndexMap.end());
    nodeIndexMap.emplace(actionHistory, nodeIndex);
    return nodeIndex;
}

std::size_t Tree::createChanceNodeRecursive(
    const IGameRules& rules,
    const GameState& state,
    const std::array<std::uint16_t, 2>& rangeSizes,
    std::vector<ActionID>& actionHistory,
    std::unordered_map<std::vector<ActionID>, std::size_t, ActionHistoryHash>& nodeIndexMap
) {
    // Recurse to child nodes
    std::vector<ActionID> validActions = rules.getValidActions(state);
    std::vector<std::size_t> nextNodeIndices;
    nextNodeIndices.reserve(validActions.size());
    for (ActionID actionID : validActions) {
        assert(rules.getActionType(actionID) == ActionType::Chance);

        CardID cardID = rules.getCardCorrespondingToChance(actionID);
        CardSet newBoard = state.currentBoard | (1 << cardID);

        GameState newState = {
            .currentBoard = newBoard,
            .playerTotalWagers = state.playerTotalWagers,
            .deadMoney = state.deadMoney,
            .playerToAct = Player::P0, // Player 0 always starts a new betting round
            .lastAction = actionID,
            .currentStreet = nextStreet(state.currentStreet), // After a card is dealt we move to the next street
            .isStartOfStreet = true,
        };

        actionHistory.push_back(actionID);
        std::size_t nextNodeIndex = createNodeRecursive(rules, newState, rangeSizes, actionHistory, nodeIndexMap);
        actionHistory.pop_back();

        nextNodeIndices.push_back(nextNodeIndex);
    }
    assert(nextNodeIndices.size() == validActions.size());

    // Fill in current node information
    ChanceNode chanceNode = {
        .board = state.currentBoard,
        .actionsOffset = allActions.size(),
        .numActions = static_cast<std::uint8_t>(validActions.size())
    };

    // Update tree information
    allActions.insert(allActions.end(), validActions.begin(), validActions.end());
    allNextNodeIndices.insert(allNextNodeIndices.end(), nextNodeIndices.begin(), nextNodeIndices.end());

    allNodes.emplace_back(chanceNode);
    return allNodes.size() - 1;
}

std::size_t Tree::createDecisionNodeRecursive(
    const IGameRules& rules,
    const GameState& state,
    const std::array<std::uint16_t, 2>& rangeSizes,
    std::vector<ActionID>& actionHistory,
    std::unordered_map<std::vector<ActionID>, std::size_t, ActionHistoryHash>& nodeIndexMap
) {
    // Recurse to child nodes
    std::vector<ActionID> validActions = rules.getValidActions(state);
    std::vector<std::size_t> nextNodeIndices;
    nextNodeIndices.reserve(validActions.size());
    for (ActionID actionID : validActions) {
        assert(rules.getActionType(actionID) == ActionType::Decision);

        GameState newState = rules.getNewStateAfterDecision(state, actionID);

        actionHistory.push_back(actionID);
        std::size_t nextNodeIndex = createNodeRecursive(rules, newState, rangeSizes, actionHistory, nodeIndexMap);
        actionHistory.pop_back();

        nextNodeIndices.push_back(nextNodeIndex);
    }
    assert(nextNodeIndices.size() == validActions.size());

    // Fill in current node information
    DecisionNode decisionNode = {
        .trainingDataOffset = trainingDataLength,
        .actionsOffset = allActions.size(),
        .numTrainingDataSets = rangeSizes[getPlayerID(state.playerToAct)],
        .numActions = static_cast<std::uint8_t>(validActions.size()),
        .player = state.playerToAct
    };

    // Update tree information
    std::uint32_t nodeTrainingDataLength = static_cast<uint32_t>(decisionNode.numTrainingDataSets) * decisionNode.numActions;
    trainingDataLength += nodeTrainingDataLength;
    allActions.insert(allActions.end(), validActions.begin(), validActions.end());
    allNextNodeIndices.insert(allNextNodeIndices.end(), nextNodeIndices.begin(), nextNodeIndices.end());

    allNodes.emplace_back(decisionNode);
    return allNodes.size() - 1;
}

std::size_t Tree::createFoldNode(const GameState& state) {
    // The reward is the amount that the folding player wagered plus any dead money
    // The folding player acted last turn
    int remaningPlayerReward = state.playerTotalWagers[getOpposingPlayerID(state.playerToAct)] + state.deadMoney;

    FoldNode foldNode = {
        .remainingPlayerReward = remaningPlayerReward,
        .remainingPlayer = state.playerToAct
    };

    allNodes.emplace_back(foldNode);
    return allNodes.size() - 1;
}

std::size_t Tree::createShowdownNode(const GameState& state) {
    // At showdown players should have wagered same amount
    assert(state.playerTotalWagers[0] == state.playerTotalWagers[1]);

    // The reward is the amount wagered plus any dead money
    int reward = state.playerTotalWagers[0] + state.deadMoney;

    ShowdownNode showdownNode{
        .board = state.currentBoard,
        .reward = reward
    };

    allNodes.emplace_back(showdownNode);
    return allNodes.size() - 1;
}

void Tree::buildFullTree() {
    assert(isTreeSkeletonBuilt() && !isFullTreeBuilt());

    allStrategySums.assign(trainingDataLength, 0.0f);
    allStrategySums.shrink_to_fit();

    allRegretSums.assign(trainingDataLength, 0.0f);
    allRegretSums.shrink_to_fit();
}