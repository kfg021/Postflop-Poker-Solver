#ifndef TREE_HPP
#define TREE_HPP

#include "game/game_types.hpp"
#include "game/game_rules.hpp"
#include "solver/node.hpp"
#include "util/fixed_vector.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

class Tree {
public:
    Tree() : rangeSize{ 0, 0 }, m_trainingDataLength{ 0 }, m_numDecisionNodes{ 0 }, m_inputOutputSize{ 0, 0 } {}

    bool isTreeSkeletonBuilt() const;
    bool isFullTreeBuilt() const;
    void buildTreeSkeleton(const IGameRules& rules);
    std::size_t getNumberOfDecisionNodes() const;
    std::size_t getTreeSkeletonSize() const;
    std::size_t estimateFullTreeSize() const;
    void buildFullTree();
    std::size_t getRootNodeIndex() const;

    // Game data
    PlayerArray<std::vector<CardSet>> rangeHands;
    PlayerArray<int> rangeSize;
    PlayerArray<std::vector<int>> sameHandIndexTable;
    int gameHandSize;

    // Vector containing all nodes
    std::vector<Node> allNodes;

    // Data for chance nodes
    std::vector<CardID> allChanceCards;
    std::vector<std::size_t> allChanceNextNodeIndices;

    // Data for decision nodes
    std::vector<ActionID> allDecisions;
    std::vector<std::size_t> allDecisionNextNodeIndices;
    std::vector<float> allStrategySums;
    std::vector<float> allRegretSums;

    // Temporary space for CFR, reset after every iteration
    std::vector<float> allStrategies;
    PlayerArray<std::vector<float>> allInputOutput;

private:
    std::size_t createNode(const IGameRules& rules, const GameState& state);
    std::size_t createChanceNode(const IGameRules& rules, const GameState& state);
    std::size_t createDecisionNode(const IGameRules& rules, const GameState& state);
    std::size_t createFoldNode(const GameState& state);
    std::size_t createShowdownNode(const GameState& state);

    std::size_t m_trainingDataLength;
    std::size_t m_numDecisionNodes;
    PlayerArray<std::size_t> m_inputOutputSize;
};

#endif // TREE_HPP