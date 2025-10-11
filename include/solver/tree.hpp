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
    Tree() : m_trainingDataLength{ 0 }, m_numDecisionNodes{ 0 }, m_cfrTemporaryDataSize{ 0, 0 } {}

    bool isTreeSkeletonBuilt() const;
    bool isFullTreeBuilt() const;
    void buildTreeSkeleton(const IGameRules& rules);
    std::size_t getNumberOfDecisionNodes() const;
    std::size_t getTreeSkeletonSize() const;
    std::size_t estimateFullTreeSize() const;
    void buildFullTree();
    std::size_t getRootNodeIndex() const;

    // Using SoA instead of AoS for performance

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

    // Temporary space for CFR input / output, reset after every iteration
    PlayerArray<std::vector<float>> allCfrTemporaryData;

private:
    std::size_t createNode(const IGameRules& rules, const GameState& state);
    std::size_t createChanceNode(const IGameRules& rules, const GameState& state);
    std::size_t createDecisionNode(const IGameRules& rules, const GameState& state);
    std::size_t createFoldNode(const GameState& state);
    std::size_t createShowdownNode(const GameState& state);

    std::size_t m_trainingDataLength;
    std::size_t m_numDecisionNodes;
    PlayerArray<std::size_t> m_cfrTemporaryDataSize;
};

#endif // TREE_HPP