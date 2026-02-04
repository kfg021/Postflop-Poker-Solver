#include "solver/tree.hpp"

#include "game/game_types.hpp"
#include "game/game_rules.hpp"
#include "game/game_utils.hpp"
#include "util/fixed_vector.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <vector>

namespace {
// sameHandIndexTable[p][i] = j iff the ith entry in player p's range is equal to the jth entry in the other player's range
// (or -1 if no such index exists)
// Used to calculate showdown and fold equity for games with two card hands
PlayerArray<std::vector<std::int16_t>> buildSameHandIndexTable(const IGameRules& rules) {
    const auto player0Hands = rules.getRangeHands(Player::P0);
    const auto player1Hands = rules.getRangeHands(Player::P1);

    int player0RangeSize = player0Hands.size();
    int player1RangeSize = player1Hands.size();

    PlayerArray<std::vector<std::int16_t>> sameHandIndexTable = {
        std::vector<std::int16_t>(player0RangeSize, -1),
        std::vector<std::int16_t>(player1RangeSize, -1)
    };

    for (int i = 0; i < player0RangeSize; ++i) {
        for (int j = i; j < player1RangeSize; ++j) {
            if (player0Hands[i] == player1Hands[j]) {
                sameHandIndexTable[Player::P0][i] = static_cast<std::int16_t>(j);
                sameHandIndexTable[Player::P1][j] = static_cast<std::int16_t>(i);
            }
        }
    }

    return sameHandIndexTable;
}

PlayerArray<std::array<std::vector<std::int16_t>, 6>> buildIsomorphicHandIndices(const IGameRules& rules) {
    auto startingIsomorphisms = rules.getChanceNodeIsomorphisms(rules.getInitialGameState().currentBoard);
    PlayerArray<std::array<std::vector<std::int16_t>, 6>> isomorphicHandIndices;

    for (const SuitEquivalenceClass& isomorphism : startingIsomorphisms) {
        for (int i = 0; i < isomorphism.size(); ++i) {
            for (int j = i + 1; j < isomorphism.size(); ++j) {
                Suit x = isomorphism[i];
                Suit y = isomorphism[j];
                int twoSuitIndex = mapTwoSuitsToIndex(x, y);

                for (Player player : { Player::P0, Player::P1 }) {
                    int playerRangeSize = rules.getRangeHands(player).size();

                    assert(isomorphicHandIndices[player][twoSuitIndex].empty());
                    isomorphicHandIndices[player][twoSuitIndex].resize(playerRangeSize);
                    for (int hand = 0; hand < playerRangeSize; ++hand) {
                        isomorphicHandIndices[player][twoSuitIndex][hand] = static_cast<std::int16_t>(rules.getHandIndexAfterSuitSwap(player, hand, x, y));
                    }
                }
            }
        }
    }

    return isomorphicHandIndices;
}

double getTotalRangeWeight(const IGameRules& rules) {
    const auto player0RangeWeights = rules.getInitialRangeWeights(Player::P0);
    const auto player1RangeWeights = rules.getInitialRangeWeights(Player::P1);

    const auto player0Hands = rules.getRangeHands(Player::P0);
    const auto player1Hands = rules.getRangeHands(Player::P1);

    int player0RangeSize = player0Hands.size();
    int player1RangeSize = player1Hands.size();

    CardSet startingBoard = rules.getInitialGameState().currentBoard;

    double totalRangeWeight = 0.0;

    for (int i = 0; i < player0RangeSize; ++i) {
        if (doSetsOverlap(player0Hands[i], startingBoard)) continue;

        for (int j = 0; j < player1RangeSize; ++j) {
            if (doSetsOverlap(player0Hands[i] | startingBoard, player1Hands[j])) continue;

            totalRangeWeight += static_cast<double>(player0RangeWeights[i]) * static_cast<double>(player1RangeWeights[j]);
        }
    }

    return totalRangeWeight;
}

void createChanceNode(const IGameRules& rules, const GameState& state, std::vector<Node>& allNodes, std::queue<GameState>& queue) {
    auto getParentSuit = [](Suit suit, const FixedVector<SuitEquivalenceClass, 4>& isomorphisms) -> Suit {
        for (SuitEquivalenceClass isomorphism : isomorphisms) {
            if (isomorphism.contains(suit)) {
                // Choose the first node to be the representative for this equivalence class
                return isomorphism[0];
            }
        }

        // Assume that nodes not present in the list are their own equivalence class
        return suit;
    };

    std::uint32_t childrenOffset = allNodes.size() + queue.size() + 1;

    // Process child nodes
    FixedVector<SuitMapping, 3> suitMappings;
    FixedVector<SuitEquivalenceClass, 4> isomorphisms = rules.getChanceNodeIsomorphisms(state.currentBoard);
    CardSet availableCards = rules.getDeck() & ~state.currentBoard;
    int numTotalChanceCards = getSetSize(availableCards);
    int numCanonicalChanceCards = 0;

    CardSet temp = availableCards;
    for (int i = 0; i < numTotalChanceCards; ++i) {
        CardID nextCard = popLowestCardFromSet(temp);

        Suit suit = getCardSuit(nextCard);
        Suit parentSuit = getParentSuit(suit, isomorphisms);

        if (suit == parentSuit) {
            // At a chance node both players should have wagered same amount
            assert(state.totalWagers[Player::P0] == state.totalWagers[Player::P1]);

            ActionID streetStart = rules.getInitialGameState().lastAction;

            GameState nextState = {
                .currentBoard = state.currentBoard | cardIDToSet(nextCard), // Add next card to board
                .totalWagers = state.totalWagers,
                .previousStreetsWager = state.totalWagers[Player::P0],
                .playerToAct = Player::P0, // Player 0 always starts a new betting round
                .lastAction = streetStart,
                .lastDealtCard = nextCard,
                .currentStreet = getNextStreet(state.currentStreet), // Advance to the next street after a chance node
            };

            ++numCanonicalChanceCards;
            queue.push(nextState);
        }
        else {
            // This card would be equivalent to a card with the same value and the parent suit
            // We can save space by not storing it

            SuitMapping mapping = { .child = suit, .parent = parentSuit };
            if (!suitMappings.contains(mapping)) {
                suitMappings.pushBack(mapping);
            }
        }
    }
    assert(temp == 0);

    // Fill in current node information
    Node chanceNode = {
        .state = state,
        .childrenOffset = childrenOffset,
        .numChildren = static_cast<std::uint8_t>(numCanonicalChanceCards),
        .nodeType = NodeType::Chance,
        .availableCards = availableCards,
        .suitMappings = suitMappings
    };
    allNodes.push_back(chanceNode);
}
} // namespace

Tree::Tree() :
    gameHandSize{ 0 },
    rangeSize{ 0, 0 },
    deadMoney{ 0 },
    totalRangeWeight{ 0.0 },
    startingStreet{ Street::Flop },
    m_trainingDataSize{ 0 },
    m_numDecisionNodes{ 0 } {
}

bool Tree::isTreeSkeletonBuilt() const {
    return !allNodes.empty();
}

bool Tree::areCfrVectorsInitialized() const {
    return !allStrategySums.empty();
}

void Tree::buildTreeSkeleton(const IGameRules& rules) {
    if (isTreeSkeletonBuilt()) {
        return;
    };

    buildAllNodes(rules);

    PlayerArray<std::span<const CardSet>> rangeHands = {
        rules.getRangeHands(Player::P0),
        rules.getRangeHands(Player::P1)
    };

    // For now only games with 1 or 2 card hands are supported
    assert(!rangeHands[Player::P0].empty());
    assert(!rangeHands[Player::P1].empty());
    gameHandSize = getSetSize(rangeHands[Player::P0][0]);
    assert(gameHandSize == 1 || gameHandSize == 2);

    rangeSize = {
        static_cast<int>(rangeHands[Player::P0].size()),
        static_cast<int>(rangeHands[Player::P1].size()),
    };

    if (gameHandSize == 2) {
        sameHandIndexTable = buildSameHandIndexTable(rules);
    }

    isomorphicHandIndices = buildIsomorphicHandIndices(rules);

    deadMoney = rules.getDeadMoney();

    // Range weight of 0 means that there are no valid combos of hands
    totalRangeWeight = getTotalRangeWeight(rules);
    assert(totalRangeWeight > 0.0);

    startingStreet = rules.getInitialGameState().currentStreet;
}

std::size_t Tree::getNumberOfDecisionNodes() const {
    assert(isTreeSkeletonBuilt());
    return m_numDecisionNodes;
}

std::size_t Tree::getTreeSkeletonSize() const {
    assert(isTreeSkeletonBuilt());

    std::size_t treeStackSize = sizeof(Tree);
    std::size_t nodesHeapSize = allNodes.capacity() * sizeof(Node);
    std::size_t sameHandIndexTableHeapSize = (sameHandIndexTable[Player::P0].capacity() + sameHandIndexTable[Player::P1].capacity()) * sizeof(std::int16_t);
    std::size_t isomorphicHandIndicesHeapSize = 0;
    for (int i = 0; i < 6; ++i) {
        isomorphicHandIndicesHeapSize += (isomorphicHandIndices[Player::P0][i].capacity() + isomorphicHandIndices[Player::P1][i].capacity()) * sizeof(std::int16_t);
    }

    return treeStackSize + nodesHeapSize + sameHandIndexTableHeapSize + isomorphicHandIndicesHeapSize;
}

std::size_t Tree::estimateFullTreeSize() const {
    assert(isTreeSkeletonBuilt());

    // allStrategySums and allRegretSums each have m_trainingDataLength elements
    std::size_t trainingDataHeapSize = (m_trainingDataSize * 2) * sizeof(float);

    return getTreeSkeletonSize() + trainingDataHeapSize;
}

void Tree::buildAllNodes(const IGameRules& rules) {
    // We build the tree using BFS so that the children of a node are adjacent in memory
    std::queue<GameState> queue;
    queue.push(rules.getInitialGameState());

    while (!queue.empty()) {
        GameState state = queue.front();
        queue.pop();

        switch (rules.getNodeType(state)) {
            case NodeType::Chance:
                createChanceNode(rules, state, allNodes, queue);
                break;

            case NodeType::Decision: {
                std::uint32_t childrenOffset = allNodes.size() + queue.size() + 1;

                // Process child nodes
                FixedVector<ActionID, MaxNumActions> validActions = rules.getValidActions(state);
                for (ActionID actionID : validActions) {
                    queue.push(rules.getNewStateAfterDecision(state, actionID));
                }

                // Fill in current node information
                Node decisionNode = {
                    .state = state,
                    .childrenOffset = childrenOffset,
                    .numChildren = static_cast<std::uint8_t>(validActions.size()),
                    .nodeType = NodeType::Decision,
                    .trainingDataOffset = m_trainingDataSize,
                };

                // Update tree
                allNodes.push_back(decisionNode);
                ++m_numDecisionNodes;
                m_trainingDataSize += rules.getInitialRangeWeights(state.playerToAct).size() * decisionNode.numChildren;

                break;
            }

            case NodeType::Fold: {
                Node foldNode = {
                    .state = state,
                    .nodeType = NodeType::Fold
                };
                allNodes.push_back(foldNode);

                break;
            }

            case NodeType::Showdown: {
                // At showdown players should have wagered same amount
                assert(state.totalWagers[Player::P0] == state.totalWagers[Player::P1]);

                // Showdowns can only happen on the river
                assert(state.currentStreet == Street::River);

                Node showdownNode = {
                    .state = state,
                    .nodeType = NodeType::Showdown
                };
                allNodes.push_back(showdownNode);

                break;
            }

            default:
                assert(false);
                break;
        }
    }

    // Free unnecessary memory - vector is done growing
    allNodes.shrink_to_fit();
}

void Tree::initCfrVectors() {
    assert(isTreeSkeletonBuilt());

    allStrategySums.assign(m_trainingDataSize, 0.0f);
    allRegretSums.assign(m_trainingDataSize, 0.0f);
}

std::size_t Tree::getRootNodeIndex() const {
    assert(isTreeSkeletonBuilt() && areCfrVectorsInitialized());
    return 0;
}