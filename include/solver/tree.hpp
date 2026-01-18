#ifndef TREE_HPP
#define TREE_HPP

#include "game/game_types.hpp"
#include "game/game_rules.hpp"
#include "util/fixed_vector.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

struct Node {
    // Used by all nodes
    GameState state;

    // Used by chance nodes and decision nodes
    std::uint32_t childrenOffset;
    std::uint8_t numChildren;

    // Used by all nodes
    NodeType nodeType;

    // Used by decision nodes only
    std::size_t trainingDataOffset;

    // Used by chance nodes only
    CardSet availableCards;
    FixedVector<SuitMapping, 3> suitMappings;
};

class Tree {
public:
    Tree();

    bool isTreeSkeletonBuilt() const;
    bool areCfrVectorsInitialized() const;
    void buildTreeSkeleton(const IGameRules& rules);
    std::size_t getNumberOfDecisionNodes() const;
    std::size_t getTreeSkeletonSize() const;
    std::size_t estimateFullTreeSize() const;
    void initCfrVectors();
    std::size_t getRootNodeIndex() const;

    // Game data
    int gameHandSize;
    PlayerArray<std::vector<CardID>> rangeHandCards;
    PlayerArray<int> rangeSize;
    PlayerArray<std::vector<std::int16_t>> sameHandIndexTable;
    int deadMoney;
    double totalRangeWeight;

    // Node data
    std::vector<Node> allNodes;
    std::vector<float> allStrategySums;
    std::vector<float> allRegretSums;

private:
    void buildAllNodes(const IGameRules& rules);

    std::size_t m_trainingDataSize;
    std::size_t m_numDecisionNodes;
};

#endif // TREE_HPP