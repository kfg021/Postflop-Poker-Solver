#ifndef NODE_HPP
#define NODE_HPP

#include "game/game_types.hpp"

#include <cstddef>
#include <cstdint>

struct ChanceNode {
    // A set containing all of the different cards that could come on the turn / river given the current board
    CardSet availableCards;

    // Offset into the allChanceCards and allChanceNextNodeIndices vectors
    std::size_t chanceDataOffset;

    // Stores mappings for isomorphic suits
    FixedVector<SuitMapping, 3> suitMappings;

    // The number of chance cards (not including isomorphic cards)
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
    Player playerToAct;
};

struct FoldNode {
    // State of the board after fold
    CardSet board;

    // How much the folding player had wagered when they folded
    int foldingPlayerWager;

    // The player who folded
    Player foldingPlayer;
};

struct ShowdownNode {
    // State of the board after end of betting
    CardSet board;

    // The amount of money wagered per player (equal for both players at showdown)
    int playerWagers;
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