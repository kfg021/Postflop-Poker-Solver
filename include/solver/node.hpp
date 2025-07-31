#ifndef NODE_HPP
#define NODE_HPP

#include "game/game_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// Data in structs is ordered from largest to smallest for packing purposes

struct ChanceNode {
    // State of the board before the next card is dealt
    CardSet board;

    // Offset into the allActions and allNextNodeIndices vectors
    std::size_t actionsOffset;

    // Equal to the number of different cards that could come on the turn / river given the current board
    std::uint8_t numActions;
};

struct DecisionNode {
    // Offset into the allStrategySums and allRegretSums vectors 
    std::size_t trainingDataOffset;

    // Offset into the allActions and allNextNodeIndices vectors
    std::size_t actionsOffset;

    // There is one set of training data for each hand in the current player's range
    std::uint16_t numTrainingDataSets;

    // Equal to the number of actions that can be taken from this node by the player
    std::uint8_t numActions;

    // Which player is currently taking an action
    Player player;
};

struct FoldNode {
    // The player who did not fold wins the reward, regardless of their hand or the board
    std::int32_t remainingPlayerReward;
    Player remainingPlayer;
};

struct ShowdownNode {
    // State of the board after end of betting
    CardSet board;

    // The winner of the showdown will profit this amount
    std::int32_t reward;

    // If we are not at the river, we need to simulate a runout
    Street street;
};

struct Node {
    Node(const ChanceNode& chanceNode_) : chanceNode(chanceNode_), nodeType(NodeType::Chance) {}
    Node(const DecisionNode& decisionNode_) : decisionNode(decisionNode_), nodeType(NodeType::Decision) {}
    Node(const FoldNode& foldNode_) : foldNode(foldNode_), nodeType(NodeType::Fold) {}
    Node(const ShowdownNode& showdownNode_) : showdownNode(showdownNode_), nodeType(NodeType::Showdown) {}

    union {
        ChanceNode chanceNode;
        DecisionNode decisionNode;
        FoldNode foldNode;
        ShowdownNode showdownNode;
    };

    const NodeType nodeType;
};

#endif // NODE_HPP