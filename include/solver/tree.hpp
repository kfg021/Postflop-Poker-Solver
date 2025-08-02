#ifndef TREE_HPP
#define TREE_HPP

#include "game/game_types.hpp"
#include "game/game_rules.hpp"
#include "solver/node.hpp"
#include "util/fixed_vector.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class Tree {
public:
    Tree() : trainingDataLength{ 0 }, numDecisionNodes{ 0 } {}

    bool isTreeSkeletonBuilt() const;
    bool isFullTreeBuilt() const;
    void buildTreeSkeleton(const IGameRules& rules, const std::array<std::uint16_t, 2>& rangeSizes);
    std::size_t getNumberOfDecisionNodes() const;
    std::size_t getTreeSkeletonSize() const;
    std::size_t estimateFullTreeSize() const;
    void buildFullTree();
    Node getRootNode() const;

    // Using SoA instead of AoS for performacne

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
    std::size_t trainingDataLength;
    std::size_t numDecisionNodes;

private:
    std::size_t createNode(
        const IGameRules& rules,
        const GameState& state,
        const std::array<std::uint16_t, 2>& rangeSizes
    );
    std::size_t createChanceNode(
        const IGameRules& rules,
        const GameState& state,
        const std::array<std::uint16_t, 2>& rangeSizes
    );
    std::size_t createDecisionNode(
        const IGameRules& rules,
        const GameState& state,
        const std::array<std::uint16_t, 2>& rangeSizes
    );
    std::size_t createFoldNode(const GameState& state);
    std::size_t createShowdownNode(const GameState& state);
};

std::size_t getTrainingDataIndex(const DecisionNode& decisionNode, std::uint16_t trainingDataSet, std::uint8_t actionIndex);
FixedVector<float, MaxNumActions> getAverageStrategy(const DecisionNode& decisionNode, const Tree& tree, std::uint16_t trainingDataSet);

#endif // TREE_HPP