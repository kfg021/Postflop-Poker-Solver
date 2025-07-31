#ifndef TREE_HPP
#define TREE_HPP

#include "game/game_types.hpp"
#include "game/game_rules.hpp"
#include "solver/node.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <variant>
#include <vector>

class Tree {
public:
    Tree() : trainingDataLength{ 0 } {}

    bool isTreeSkeletonBuilt() const;
    bool isFullTreeBuilt() const;
    void buildTreeSkeleton(const IGameRules& rules, const std::array<std::uint16_t, 2>& rangeSizes);
    std::size_t getTreeSkeletonSize() const;
    std::size_t estimateFullTreeSize() const;
    void buildFullTree();
    Node getRootNode() const;

    std::vector<Node> allNodes;
    std::vector<ActionID> allActions;
    std::vector<std::size_t> allNextNodeIndices;
    std::vector<float> allStrategySums;
    std::vector<float> allRegretSums;
    std::size_t trainingDataLength;

private:
    struct ActionHistoryHash {
        std::size_t operator()(const std::vector<ActionID>& actionHistory) const;
    };

    std::size_t createNodeRecursive(
        const IGameRules& rules,
        const GameState& state,
        const std::array<std::uint16_t, 2>& rangeSizes,
        std::vector<ActionID>& actionHistory,
        std::unordered_map<std::vector<ActionID>, std::size_t, ActionHistoryHash>& nodeIndexMap
    );
    std::size_t createChanceNodeRecursive(
        const IGameRules& rules,
        const GameState& state,
        const std::array<std::uint16_t, 2>& rangeSizes,
        std::vector<ActionID>& actionHistory,
        std::unordered_map<std::vector<ActionID>, std::size_t, ActionHistoryHash>& nodeIndexMap
    );
    std::size_t createDecisionNodeRecursive(
        const IGameRules& rules,
        const GameState& state,
        const std::array<std::uint16_t, 2>& rangeSizes,
        std::vector<ActionID>& actionHistory,
        std::unordered_map<std::vector<ActionID>, std::size_t, ActionHistoryHash>& nodeIndexMap
    );
    std::size_t createFoldNode(const GameState& state);
    std::size_t createShowdownNode(const GameState& state);
};

#endif // TREE_HPP