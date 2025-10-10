#ifndef NODE_HPP
#define NODE_HPP

#include "game/game_types.hpp"

#include <cstddef>
#include <cstdint>

// Data in structs is ordered from largest to smallest for packing purposes

struct ChanceNode {
    // State of the board before the next card is dealt
    CardSet board;

    // Offset into the allChanceCards and allChanceNextNodeIndices vectors
    std::size_t chanceDataOffset;

    // Equal to the number of different cards that could come on the turn / river given the current board
    std::uint8_t chanceDataSize;
};

struct DecisionNode {
    // Offset into the allStrategySums and allRegretSums vectors 
    std::size_t trainingDataOffset;

    // Offset into the allDecisions and allDecisionNextNodeIndices vectors
    std::size_t decisionDataOffset;

    // Equal to the number of actions that can be taken from this node by the player
    std::uint8_t decisionDataSize;

    // Which player is currently taking an action
    Player player;
};

struct FoldNode {
    // State of the board after fold
    CardSet board;

    // The player who did not fold wins the reward, regardless of their hand or the board
    int remainingPlayerReward;
    Player remainingPlayer;
};

struct ShowdownNode {
    // State of the board after end of betting
    CardSet board;

    // The winner of the showdown will profit this amount
    int reward;
};

class Node {
public:
    Node(const ChanceNode& chanceNode_) : chanceNode(chanceNode_), m_nodeType(NodeType::Chance) {}
    Node(const DecisionNode& decisionNode_) : decisionNode(decisionNode_), m_nodeType(NodeType::Decision) {}
    Node(const FoldNode& foldNode_) : foldNode(foldNode_), m_nodeType(NodeType::Fold) {}
    Node(const ShowdownNode& showdownNode_) : showdownNode(showdownNode_), m_nodeType(NodeType::Showdown) {}

    NodeType getNodeType() const { return m_nodeType; }

    union {
        ChanceNode chanceNode;
        DecisionNode decisionNode;
        FoldNode foldNode;
        ShowdownNode showdownNode;
    };

private:
    NodeType m_nodeType;
};

#endif // NODE_HPP