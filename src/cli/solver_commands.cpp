#include "cli/solver_commands.hpp"

#include "cli/cli_dispatcher.hpp"
#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "game/holdem/holdem_parser.hpp"
#include "game/holdem/holdem.hpp"
#include "game/kuhn_poker.hpp"
#include "game/leduc_poker.hpp"
#include "solver/cfr.hpp"
#include "solver/tree.hpp"
#include "util/fixed_vector.hpp"
#include "util/result.hpp"
#include "util/scoped_timer.hpp"
#include "util/stack_allocator.hpp"
#include "util/string_utils.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <optional>
#include <memory>
#include <optional>
#include <string>

#include <yaml-cpp/yaml.h>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {
bool isContextValid(SolverContext& context) {
    return context.rules != nullptr;
}

void printInvalidContextError() {
    std::cerr << "Error: Game settings not loaded. Please run \"kuhn\", \"leduc\", or \"holdem <file>\" first.\n";
}

void buildTreeSkeletonIfNeeded(SolverContext& context) {
    assert(isContextValid(context));

    if (!context.tree->isTreeSkeletonBuilt()) {
        {
            ScopedTimer timer{ "Tree skeleton not yet built, building...", "Finished building tree skeleton" };
            context.tree->buildTreeSkeleton(*context.rules);
        }
        std::cout << "\n";
    }

}

bool isTreeSolved(SolverContext& context) {
    return isContextValid(context) && context.tree->areCfrVectorsInitialized();
}

void printUnsolvedTreeError() {
    std::cerr << "Error: Tree must be solved first.\n";
}

template <typename T>
bool loadField(T& field, const YAML::Node& node, const std::vector<std::string>& indices, int depth) {
    if (!node.IsDefined() || node.IsNull()) {
        return false;
    }

    if (depth == indices.size()) {
        try {
            field = node.as<T>();
            std::cout << "Successfully loaded field " << join(indices, "::") << ".\n";
            return true;
        }
        catch (const YAML::Exception&) {
            return false;
        }
    }

    return loadField(field, node[indices[depth]], indices, depth + 1);
}

template <typename T>
bool loadRequiredField(T& field, const YAML::Node& root, const std::vector<std::string>& indices) {
    bool success = loadField(field, root, indices, 0);
    if (!success) {
        std::cerr << "Error: Could not load field " << join(indices, "::") << ".\n";
        return false;
    }

    return true;
}

template <typename T>
void loadOptionalField(T& field, const YAML::Node& root, const std::vector<std::string>& indices, const T& defaultValue) {
    bool success = loadField(field, root, indices, 0);
    if (!success) {
        std::cout << "Could not load field " << join(indices, "::") << ", using default.\n";
        field = defaultValue;
    }
}

void loadOptionalIntWithBounds(int& field, const YAML::Node& root, const std::vector<std::string>& indices, int defaultValue, std::optional<int> lowerBound, std::optional<int> upperBound) {
    if (lowerBound) {
        assert(defaultValue >= *lowerBound);
    }
    if (upperBound) {
        assert(defaultValue <= *upperBound);
    }

    loadOptionalField<int>(field, root, indices, defaultValue);

    bool belowLowerBound = lowerBound && (field < *lowerBound);
    bool aboveUpperBound = upperBound && (field > *upperBound);

    if (belowLowerBound || aboveUpperBound) {
        field = defaultValue;

        std::cout << "Value provided for field " << join(indices, "::") << " was ";
        if (belowLowerBound) {
            std::cout << "below the minimum value of " << *lowerBound << ",";
        }
        else {
            std::cout << "above the maximum value of " << *upperBound << ",";
        }
        std::cout << " using default.\n";
    }
}

template <typename T, std::size_t Capacity>
bool fillFixedVector(FixedVector<T, Capacity>& fixedVec, const std::vector<T>& vec) {
    if (vec.size() > Capacity) {
        return false;
    }

    for (const T& elem : vec) {
        fixedVec.pushBack(elem);
    }
    return true;
}

std::string removeOuterQuotes(const std::string& input) {
    int inputSize = input.size();
    if (inputSize < 2) {
        return input;
    }

    if ((input.front() == '\'' || input.front() == '\"') && input.front() == input.back()) {
        return input.substr(1, inputSize - 2);
    }
    else {
        return input;
    }
}

bool handleSetupHoldem(SolverContext& context, const std::string& argument) {
    std::string filePath = removeOuterQuotes(argument);

    YAML::Node input;
    try {
        input = YAML::LoadFile(filePath);
    }
    catch (const YAML::Exception& e) {
        std::cerr << "Error: Could not load settings file. " << e.what() << "\n";
        return false;
    }

    std::cout << "Loading Holdem settings from " << filePath << ":\n";

    static constexpr PlayerArray<std::string> playerNames = { "oop", "ip" };
    static constexpr StreetArray<std::string> streetNames = { "flop", "turn", "river" };

    Holdem::Settings settings;

    // Load board
    std::string boardString;
    if (!loadRequiredField(boardString, input, { "board" })) {
        return false;
    }
    Result<CardSet> boardResult = buildCommunityCardsFromString(boardString);
    if (boardResult.isError()) {
        std::cerr << boardResult.getError() << "\n";
        return false;
    }
    settings.startingCommunityCards = boardResult.getValue();

    // Load ranges
    for (Player player : { Player::P0, Player::P1 }) {
        std::string rangeString;
        if (!loadRequiredField(rangeString, input, { "ranges", playerNames[player] })) {
            return false;
        }
        Result<Holdem::Range> rangeResult = buildRangeFromString(rangeString, settings.startingCommunityCards);
        if (rangeResult.isError()) {
            std::cerr << rangeResult.getError() << "\n";
            return false;
        }
        settings.ranges[player] = rangeResult.getValue();
    }

    // Tree settings
    // Load bet and raise sizes
    for (Player player : { Player::P0, Player::P1 }) {
        for (Street street : { Street::Flop, Street::Turn, Street::River }) {
            {
                std::vector<int> betSizesVector;
                loadOptionalField(betSizesVector, input, { "tree", "actions", playerNames[player], streetNames[street], "bet-sizes" }, {});
                if (!fillFixedVector(settings.betSizes[player][street], betSizesVector)) {
                    std::cerr << "Error: Too many bet sizes provided for " << playerNames[player] << " " << streetNames[street] << ", maximum is " << holdem::MaxNumBetSizes << "\n.";
                    return false;
                }
            }

            {
                std::vector<int> raiseSizesVector;
                loadOptionalField(raiseSizesVector, input, { "tree", "actions", playerNames[player], streetNames[street], "raise-sizes" }, {});
                if (!fillFixedVector(settings.raiseSizes[player][street], raiseSizesVector)) {
                    std::cerr << "Error: Too many raise sizes provided for " << playerNames[player] << " " << streetNames[street] << ", maximum is " << holdem::MaxNumRaiseSizes << "\n.";
                    return false;
                }
            }
        }
    }

    // Load starting wager
    if (!loadRequiredField(settings.startingPlayerWagers, input, { "tree", "starting-wager-per-player" })) {
        return false;
    }
    if (settings.startingPlayerWagers <= 0) {
        std::cerr << "Error: Starting wager per player must be positive.\n";
        return false;
    }

    // Load effective stack
    if (!loadRequiredField(settings.effectiveStackRemaining, input, { "tree", "effective-stack-remaining" })) {
        return false;
    }
    if (settings.effectiveStackRemaining <= 0) {
        std::cerr << "Error: Effective stack must be positive.\n";
        return false;
    }

    // Load dead money
    loadOptionalIntWithBounds(settings.deadMoney, input, { "tree", "dead-money-in-pot" }, 0, 0, std::nullopt);

    // Load use isomorphism
    loadOptionalField(settings.useChanceCardIsomorphism, input, { "tree", "use-isomorphism" }, true);

    // Solver settings
    // Load num threads
    #ifdef _OPENMP
    constexpr static int MaxNumThreads = 64;
    int defaultNumThreads = std::min(omp_get_max_threads(), MaxNumThreads);
    loadOptionalIntWithBounds(context.numThreads, input, { "solver", "threads" }, defaultNumThreads, 1, MaxNumThreads);
    #else
    context.numThreads = 1;
    std::cout << "OpenMP was not found, using one thread.\n";
    #endif

    // Load target exploitability
    loadOptionalField(context.targetPercentExploitability, input, { "solver", "target-exploitability" }, 0.3f);

    // Load max iterations
    loadOptionalIntWithBounds(context.maxIterations, input, { "solver", "max-iterations" }, 1000, 1, std::nullopt);

    // Load exploitability check frequency
    loadOptionalIntWithBounds(context.exploitabilityCheckFrequency, input, { "solver", "exploitability-check-frequency" }, 10, 1, std::nullopt);

    std::cout << "Successfully loaded Holdem settings.\n\n";

    {
        ScopedTimer timer{ "Building Holdem lookup tables...", "Finished building lookup tables" };
        context.rules = std::make_unique<Holdem>(settings);
    }

    context.tree = std::make_unique<Tree>();

    return true;
}

bool handleSetupKuhn(SolverContext& context) {
    context = {
        .rules = std::make_unique<KuhnPoker>(),
        .tree = std::make_unique<Tree>(),
        .targetPercentExploitability = 0.3f,
        .maxIterations = 100000,
        .exploitabilityCheckFrequency = 10000,
        .numThreads = 1
    };

    std::cout << "Successfully loaded Kuhn poker.\n";
    return true;
}

bool handleSetupLeduc(SolverContext& context) {
    static constexpr bool UseChanceCardIsomorphism = true;

    context = {
        .rules = std::make_unique<LeducPoker>(UseChanceCardIsomorphism),
        .tree = std::make_unique<Tree>(),
        .targetPercentExploitability = 0.3f,
        .maxIterations = 10000,
        .exploitabilityCheckFrequency = 1000,
        .numThreads = 1
    };

    std::cout << "Successfully loaded Leduc poker.\n";
    return true;
}

bool handleEstimateTreeSize(SolverContext& context) {
    if (!isContextValid(context)) {
        printInvalidContextError();
        return false;
    }

    buildTreeSkeletonIfNeeded(context);

    std::cout << "Total number of nodes: " << context.tree->allNodes.size() << "\n";
    std::cout << "Number of decision nodes: " << context.tree->getNumberOfDecisionNodes() << "\n";
    std::cout << "Tree skeleton size: " << formatBytes(context.tree->getTreeSkeletonSize()) << "\n";
    std::cout << "Expected full tree size: " << formatBytes(context.tree->estimateFullTreeSize()) << "\n";
    return true;
}

bool handleNodeInfo(SolverContext& context) {
    auto getNodeTypeString = [](NodeType nodeType) -> std::string {
        switch (nodeType) {
            case NodeType::Chance:
                return "Chance";
            case NodeType::Decision:
                return "Decision";
            case NodeType::Fold:
                return "Fold";
            case NodeType::Showdown:
                return "Showdown";
            default:
                assert(false);
                return "???";
        }
    };

    auto getBoardString = [&context]() -> std::string {
        // First get the cards from the starting board
        std::vector<std::string> boardCards = getCardSetNames(context.rules->getInitialGameState().currentBoard);

        // Then get turn/river cards, applying suit swap lists if needed
        CardID lastChanceCard = InvalidCard;
        std::optional<SuitMapping> lastSwapList;
        for (const auto& [index, swapList] : context.nodePath) {
            const Node& currentNode = context.tree->allNodes[index];
            CardID lastDealtCard = currentNode.state.lastDealtCard;
            if (lastDealtCard != lastChanceCard) {
                // We've reached a new chance card, add it to the board after applying swap lists
                // To go from tree suits to user suits, we need to apply the swaps in reverse order
                // There can be at most 2 swap lists, one for turn and one for river
                CardID cardToAdd = lastDealtCard;
                if (swapList) {
                    cardToAdd = swapCardSuits(cardToAdd, swapList->child, swapList->parent);
                }
                if (lastSwapList) {
                    cardToAdd = swapCardSuits(cardToAdd, lastSwapList->child, lastSwapList->parent);
                }
                boardCards.push_back(getNameFromCardID(cardToAdd));

                lastChanceCard = lastDealtCard;
                lastSwapList = swapList;
            }
        }

        if (boardCards.empty()) {
            return "Empty";
        }
        else {
            return join(boardCards, " ");
        }
    };

    auto getActionString = [&context](int action) -> std::string {
        assert(!context.nodePath.empty());
        const Node& node = context.tree->allNodes[context.nodePath.back().index];
        const Node& nextNode = context.tree->allNodes[node.childrenOffset + action];

        int lastBetTotal = std::max(nextNode.state.totalWagers[Player::P0], nextNode.state.totalWagers[Player::P1]);
        int betOrRaiseSize = lastBetTotal - node.state.previousStreetsWager;

        return context.rules->getActionName(nextNode.state.lastAction, betOrRaiseSize);
    };

    if (!isContextValid(context)) {
        printInvalidContextError();
        return false;
    }

    if (!isTreeSolved(context)) {
        printUnsolvedTreeError();
        return false;
    }

    static constexpr PlayerArray<std::string> playerNames = { "OOP", "IP" };

    assert(!context.nodePath.empty());
    const Node& node = context.tree->allNodes[context.nodePath.back().index];

    int oopWager = node.state.totalWagers[Player::P0];
    int ipWager = node.state.totalWagers[Player::P1];
    int deadMoney = context.tree->deadMoney;

    // TODO: Print series of events that led to this node
    std::cout << "Node type: " << getNodeTypeString(node.nodeType) << "\n";
    std::cout << "Board: " << getBoardString() << "\n";
    std::cout << "OOP wager: " << oopWager << "\n";
    std::cout << "IP wager: " << ipWager << "\n";
    if (deadMoney > 0) {
        std::cout << "Dead money in pot: " << deadMoney << "\n";
    }
    std::cout << "Total pot size: " << oopWager + ipWager + deadMoney << "\n";

    switch (node.nodeType) {
        case NodeType::Chance: {
            std::cout << "Possible cards: ";
            int numTotalChanceCards = getSetSize(node.availableCards);
            CardSet temp = node.availableCards;
            for (int i = 0; i < numTotalChanceCards; ++i) {
                std::cout << getNameFromCardID(popLowestCardFromSet(temp)) << " ";
            }
            assert(temp == 0);
            std::cout << "\n";

            return true;
        }

        case NodeType::Decision:
            std::cout << "Player to act: " << playerNames[node.state.playerToAct] << "\n";
            for (int action = 0; action < node.numChildren; ++action) {
                std::cout << "    [" << action << "] " << getActionString(action) << "\n";
            }

            return true;

        case NodeType::Fold:
            std::cout << playerNames[node.state.playerToAct] << " wins\n";
            return true;

        case NodeType::Showdown:
            return true;

        default:
            assert(false);
            return false;

    }
}

bool handleRoot(SolverContext& context) {
    if (!isContextValid(context)) {
        printInvalidContextError();
        return false;
    }

    if (!isTreeSolved(context)) {
        printUnsolvedTreeError();
        return false;
    }

    context.nodePath = { { context.tree->getRootNodeIndex(), std::nullopt } };

    // Print node info for root node
    return handleNodeInfo(context);
}

bool handleSolve(SolverContext& context) {
    struct CfrResult {
        float exploitability;
        int iteration;
    };

    auto getStartingPot = [&context]() -> int {
        GameState initialState = context.rules->getInitialGameState();
        return initialState.totalWagers[Player::P0] + initialState.totalWagers[Player::P1] + context.tree->deadMoney;
    };

    auto runCfr = [&context, &getStartingPot](StackAllocator<float>& allocator) -> std::optional<CfrResult> {
        std::optional<CfrResult> resultOption;
        float startingPot = static_cast<float>(getStartingPot());

        for (int i = 0; i < context.maxIterations; ++i) {
            int iteration = i + 1;

            for (Player hero : { Player::P0, Player::P1 }) {
                // Using Discounted CFR with alpha = 1.5, beta = 0, gamma = 2
                // These values work very well in practice, as shown in below paper

                // Brown, N., & Sandholm, T. (2019). 
                // Solving Imperfect-Information Games via Discounted Regret Minimization. 
                // Proceedings of the AAAI Conference on Artificial Intelligence, 33(01), 1829-1836. 
                // https://doi.org/10.1609/aaai.v33i01.33011829

                discountedCfr(hero, *context.rules, getDiscountParams(1.5f, 0.0f, 2.0f, iteration), *context.tree, allocator);
            }

            if ((context.exploitabilityCheckFrequency > 0) && (iteration % context.exploitabilityCheckFrequency == 0)) {
                float exploitability = calculateExploitabilityFast(*context.rules, *context.tree, allocator);
                float exploitabilityPercent = (exploitability / startingPot) * 100.0f;
                std::cout << "Finished iteration " << iteration << ". Exploitability: " << formatFixedPoint(exploitability, 5) << " (" << formatFixedPoint(exploitabilityPercent, 5) << "%)\n";
                if (exploitabilityPercent <= context.targetPercentExploitability) {
                    resultOption = CfrResult{ exploitability, iteration };
                    break;
                }
            }
        }

        return resultOption;
    };

    if (!isContextValid(context)) {
        printInvalidContextError();
        return false;
    }

    buildTreeSkeletonIfNeeded(context);

    {
        ScopedTimer timer{ "Allocating memory...", "Finished allocating memory" };
        context.tree->initCfrVectors();
    }
    std::cout << "\n";

    std::optional<CfrResult> resultOption;

    #ifdef _OPENMP
    omp_set_num_threads(context.numThreads);
    StackAllocator<float> allocator(context.numThreads);

    #pragma omp parallel
    {
        #pragma omp single
        {
            std::cout << "Starting training in parallel with " << omp_get_num_threads() << " threads. Target exploitability: "
                << formatFixedPoint(context.targetPercentExploitability, 5)
                << "% Maximum iterations: " << context.maxIterations << "\n" << std::flush;

            {
                ScopedTimer timer{ {}, "Finished training" };
                resultOption = runCfr(allocator);
            }
        }
    }
    #else
    context.numThreads = 1;
    StackAllocator<float> allocator(context.numThreads);
    std::cout << "Starting training in single-threaded mode. Target exploitability: "
        << formatFixedPoint(context.targetPercentExploitability, 5)
        << "% Maximum iterations: " << context.maxIterations << "\n" << std::flush;

    {
        ScopedTimer timer{ {}, "Finished training" };
        resultOption = runCfr(allocator);
    }
    #endif

    if (resultOption) {
        std::cout << "Target exploitability percentage reached after iteration " << resultOption->iteration << ".\n\n";
}
    else {
        std::cout << "Target exploitability percentage not reached.\n\n";
    }

    std::cout << "Calculating expected value of final strategy...\n" << std::flush;
    float player0ExpectedValue = expectedValue(Player::P0, *context.rules, *context.tree, allocator);
    float player1ExpectedValue = expectedValue(Player::P1, *context.rules, *context.tree, allocator);
    std::cout << "Player 0 expected value: " << formatFixedPoint(player0ExpectedValue, 5) << "\n";
    std::cout << "Player 1 expected value: " << formatFixedPoint(player1ExpectedValue, 5) << "\n\n";

    std::cout << "Calculating exploitability of final strategy...\n" << std::flush;
    float exploitability = resultOption ? resultOption->exploitability : calculateExploitabilityFast(*context.rules, *context.tree, allocator);
    float exploitabilityPercent = (exploitability / static_cast<float>(getStartingPot())) * 100.0f;
    std::cout << "Exploitability: " << formatFixedPoint(exploitability, 5) << " (" << formatFixedPoint(exploitabilityPercent, 5) << "%)\n\n";

    std::cout << "Maximum stack allocator memory usage per thread: ";
    auto stackUsages = allocator.getMaximumStackUsage();
    for (int i = 0; i < context.numThreads; ++i) {
        std::cout << formatBytes(stackUsages[i]);
        if (i < context.numThreads - 1) std::cout << ", ";
    }
    std::cout << "\n\n";

    // Start traversal at the root
    return handleRoot(context);
}

bool handleStrategy(SolverContext& context, const std::string& argument) {
    struct Strategy {
        CardSet hand;
        double weight;
        FixedVector<float, MaxNumActions> finalStrategy;
    };

    auto getStrategyForHand = [&context](CardSet hand) -> std::optional<Strategy> {
        const Node& node = context.tree->allNodes[context.nodePath.back().index];
        Player playerToAct = node.state.playerToAct;
        const auto rangeHands = context.rules->getRangeHands(playerToAct);

        // Find out which index in the current player's range this hand corresponds to
        int handIndex = -1;
        for (int i = 0; i < rangeHands.size(); ++i) {
            if (hand == rangeHands[i]) {
                handIndex = i;
                break;
            }
        }
        if (handIndex == -1) {
            // Hand was not in our range
            return std::nullopt;
        }

        if (doSetsOverlap(hand, context.rules->getInitialGameState().currentBoard)) {
            // Hand is in our range, but is blocked by the starting board
            return std::nullopt;
        }

        double handWeight = static_cast<double>(context.rules->getInitialRangeWeights(playerToAct)[handIndex]);

        assert(!context.nodePath.empty());
        for (int i = 0; i < static_cast<int>(context.nodePath.size()) - 1; ++i) {
            const Node& currentNode = context.tree->allNodes[context.nodePath[i].index];
            const Node& nextNode = context.tree->allNodes[context.nodePath[i + 1].index];
            std::optional<SuitMapping> swapList = context.nodePath[i + 1].swapList;

            switch (currentNode.nodeType) {
                case NodeType::Chance: {
                    if (swapList) {
                        // We need to swap our hand index to reflect the swapped suits
                        const auto& isomorphicHandIndices = context.tree->isomorphicHandIndices[playerToAct][mapTwoSuitsToIndex(swapList->parent, swapList->child)];
                        assert(isomorphicHandIndices.size() == context.tree->rangeSize[playerToAct]);
                        handIndex = isomorphicHandIndices[handIndex];
                    }

                    // Exit if the most recently added chance card overlaps with our hand
                    CardID lastDealtCard = nextNode.state.lastDealtCard;
                    if (setContainsCard(rangeHands[handIndex], lastDealtCard)) {
                        return std::nullopt;
                    }

                    break;
                }

                case NodeType::Decision:
                    assert(!swapList);
                    if (currentNode.state.playerToAct == playerToAct) {
                        // This is a strategy node for the current player, multiply the hand weight by the strategy for the action we took
                        int actionIndexTaken = context.nodePath[i + 1].index - currentNode.childrenOffset;
                        assert((actionIndexTaken >= 0) && (actionIndexTaken < currentNode.numChildren));
                        FixedVector<float, MaxNumActions> finalStrategy = getFinalStrategy(handIndex, currentNode, *context.tree);
                        handWeight *= static_cast<double>(finalStrategy[actionIndexTaken]);
                    }
                    break;

                case NodeType::Fold:
                case NodeType::Showdown:
                default:
                    assert(false);

            }
        }

        return Strategy{ hand, handWeight, getFinalStrategy(handIndex, node, *context.tree) };
    };

    if (!isContextValid(context)) {
        printInvalidContextError();
        return false;
    }

    if (!isTreeSolved(context)) {
        printUnsolvedTreeError();
        return false;
    }

    assert(!context.nodePath.empty());
    const Node& node = context.tree->allNodes[context.nodePath.back().index];
    if (node.nodeType != NodeType::Decision) {
        std::cerr << "Error: Current node is not a decision node.\n";
        return false;
    }

    std::vector<Strategy> strategies;
    if (argument == "all") {
        for (CardSet hand : context.rules->getRangeHands(node.state.playerToAct)) {
            std::optional<Strategy> strategyOption = getStrategyForHand(hand);
            if (strategyOption) {
                strategies.push_back(*strategyOption);
            }
        }
    }
    else {
        switch (context.tree->gameHandSize) {
            case 1: {
                if (argument.size() != 1) {
                    std::cerr << "Error: Hand classes for one card hands must be one character (Ex. K, Q, J).\n";
                    return false;
                }

                Result<Value> cardValueResult = getValueFromChar(argument[0]);
                if (cardValueResult.isError()) {
                    std::cerr << cardValueResult.getError() << "\n";
                    return false;
                }

                for (int suit = 3; suit >= 0; --suit) {
                    CardID card = getCardIDFromValueAndSuit(cardValueResult.getValue(), static_cast<Suit>(suit));
                    CardSet hand = cardIDToSet(card);
                    std::optional<Strategy> strategyOption = getStrategyForHand(hand);
                    if (strategyOption) {
                        strategies.push_back(*strategyOption);
                    }
                }

                break;
            }

            case 2: {
                Result<std::vector<CardSet>> handClassResult = getHandClassFromString(argument);
                if (handClassResult.isError()) {
                    std::cerr << handClassResult.getError() << "\n";
                    return false;
                }

                for (CardSet hand : handClassResult.getValue()) {
                    std::optional<Strategy> strategyOption = getStrategyForHand(hand);
                    if (strategyOption) {
                        strategies.push_back(*strategyOption);
                    }
                }

                break;
            }

            default:
                assert(false);
                return false;
        }
    }

    if (strategies.empty()) {
        std::cerr << "Error: Hand class " << argument << " is not present in the current player's range or is blocked by the board.\n";
        return false;
    }

    // Print the final strategy

    auto extendString = [](const std::string& input, int totalSize) -> std::string {
        int currentSize = input.size();

        if (currentSize >= totalSize) {
            return input;
        }
        else {
            return input + std::string(totalSize - currentSize, ' ');
        }
    };

    auto printDivider = [&node]() -> void {
        std::cout << "+------+---------+";
        for (int i = 0; i < node.numChildren; ++i) {
            std::cout << "-------+";
        }
        std::cout << "\n";
    };

    double totalWeight = 0.0;
    FixedVector<double, MaxNumActions> totalStrategy(node.numChildren, 0.0);

    // Print the header
    printDivider();

    std::cout << "| Hand | Weight  |";
    for (int i = 0; i < node.numChildren; ++i) {
        std::cout << " [" << i << "]   |";
    }
    std::cout << "\n";

    printDivider();

    // Print the rows
    for (const auto& [hand, weight, strategy] : strategies) {
        std::string handString = join(getCardSetNames(hand), "");
        std::cout << "| " << extendString(handString, 5) << "| " << formatFixedPoint(weight, 3) << "   |";

        assert(strategy.size() == node.numChildren);
        for (int i = 0; i < node.numChildren; ++i) {
            std::cout << " " << formatFixedPoint(strategy[i], 3) << " |";

            totalStrategy[i] += strategy[i] * weight;
        }
        std::cout << "\n";

        totalWeight += weight;
    }

    printDivider();

    // Print the total strategy
    std::cout << "| " << extendString(argument, 5) << "| " << extendString(formatFixedPoint(totalWeight, 3), 8) << "|";
    for (int i = 0; i < node.numChildren; ++i) {
        std::cout << " " << formatFixedPoint(totalStrategy[i] / totalWeight, 3) << " |";
    }
    std::cout << "\n";

    printDivider();

    return true;
}

bool handleAction(SolverContext& context, const std::string& argument) {
    if (!isContextValid(context)) {
        printInvalidContextError();
        return false;
    }

    if (!isTreeSolved(context)) {
        printUnsolvedTreeError();
        return false;
    }

    assert(!context.nodePath.empty());
    const Node& node = context.tree->allNodes[context.nodePath.back().index];
    if (node.nodeType != NodeType::Decision) {
        std::cerr << "Error: Current node is not a decision node.\n";
        return false;
    }

    std::optional<int> actionOption = parseInt(argument);
    if (!actionOption) {
        std::cerr << "Error: Action is not a valid integer.\n";
        return false;
    }

    int action = *actionOption;
    if (action < 0 || action >= node.numChildren) {
        std::cerr << "Error: Action id is out of range.\n";
        return false;
    }

    context.nodePath.push_back({ node.childrenOffset + action, std::nullopt });

    // Print node info for new node
    return handleNodeInfo(context);
}

bool handleDeal(SolverContext& context, const std::string& argument) {
    if (!isContextValid(context)) {
        printInvalidContextError();
        return false;
    }

    if (!isTreeSolved(context)) {
        printUnsolvedTreeError();
        return false;
    }

    assert(!context.nodePath.empty());
    const Node& node = context.tree->allNodes[context.nodePath.back().index];
    if (node.nodeType != NodeType::Chance) {
        std::cerr << "Error: Current node is not a chance node.\n";
        return false;
    }

    Result<CardID> cardResult = getCardIDFromName(argument);
    if (cardResult.isError()) {
        std::cerr << cardResult.getError() << "\n";
        return false;
    }

    CardID dealCard = cardResult.getValue();
    if (!setContainsCard(node.availableCards, dealCard)) {
        std::cerr << "Error: Card is not available to be dealt.\n";
        return false;
    }

    // Apply swap list from previous nodes
    // There can only be one, since at most the turn could have happened before this
    for (const auto& [index, swapList] : context.nodePath) {
        if (swapList) {
            dealCard = swapCardSuits(dealCard, swapList->child, swapList->parent);
            break;
        }
    }

    // Because of isomorphism, the card might not actually exist in the tree
    std::optional<SuitMapping> swapList;
    for (SuitMapping mapping : node.suitMappings) {
        if (getCardSuit(dealCard) == mapping.child) {
            swapList = mapping;
            break;
        }
    }

    CardID isomorphicDealCard;
    if (swapList) {
        isomorphicDealCard = getCardIDFromValueAndSuit(getCardValue(dealCard), swapList->parent);
    }
    else {
        isomorphicDealCard = dealCard;
    }

    for (int cardIndex = 0; cardIndex < node.numChildren; ++cardIndex) {
        CardID card = context.tree->allNodes[node.childrenOffset + cardIndex].state.lastDealtCard;
        assert(card != InvalidCard);

        if (card == isomorphicDealCard) {
            context.nodePath.push_back({ node.childrenOffset + cardIndex, swapList });

            // Print node info for new node
            return handleNodeInfo(context);
        }
    }

    assert(false);
    return false;
}

bool handleBack(SolverContext& context) {
    if (!isContextValid(context)) {
        printInvalidContextError();
        return false;
    }

    if (!isTreeSolved(context)) {
        printUnsolvedTreeError();
        return false;
    }

    assert(!context.nodePath.empty());
    if (context.nodePath.size() == 1) {
        std::cerr << "Error: Already at root.\n";
        return false;
    }

    context.nodePath.pop_back();

    // Print node info for new node
    return handleNodeInfo(context);
}
} // namespace

bool registerAllCommands(CliDispatcher& dispatcher, SolverContext& context) {
    bool allSuccess = true;

    allSuccess &= dispatcher.registerCommand(
        "holdem",
        "file",
        "Loads Holdem game settings from a given .yml configuration file.",
        [&context](const std::string& argument) { return handleSetupHoldem(context, argument); }
    );

    allSuccess &= dispatcher.registerCommand(
        "kuhn",
        "Loads settings for Kuhn poker, a simplified version of poker with three possible hands and one betting round.",
        [&context]() { return handleSetupKuhn(context); }
    );

    allSuccess &= dispatcher.registerCommand(
        "leduc",
        "Loads settings for Leduc poker, a simplified version of poker with six possible hands and two betting rounds.",
        [&context]() { return handleSetupLeduc(context); }
    );

    allSuccess &= dispatcher.registerCommand(
        "size",
        "Provides an estimate of the size of the tree.",
        [&context]() { return handleEstimateTreeSize(context); }
    );

    allSuccess &= dispatcher.registerCommand(
        "solve",
        "Solves the game tree using Discounted CFR. It is recommended to first run \"tree-size\" to ensure that the tree fits in RAM.",
        [&context]() { return handleSolve(context); }
    );

    allSuccess &= dispatcher.registerCommand(
        "info",
        "Prints information about the current node.",
        [&context]() { return handleNodeInfo(context); }
    );

    allSuccess &= dispatcher.registerCommand(
        "strategy",
        "hand-class",
        "Prints the optimal strategy for a particular hand class (ex. AA, AKo, JTs), or for the entire range (all).",
        [&context](const std::string& argument) { return handleStrategy(context, argument); }
    );

    allSuccess &= dispatcher.registerCommand(
        "action",
        "id",
        "Simulates playing the action corresponding to the given id. Valid actions can be found by running \"info\" for decision nodes only.",
        [&context](const std::string& argument) { return handleAction(context, argument); }
    );

    allSuccess &= dispatcher.registerCommand(
        "deal",
        "card",
        "Deals the given card at a chance node. Valid cards can be found by running \"info\" for chance nodes only.",
        [&context](const std::string& argument) { return handleDeal(context, argument); }
    );

    allSuccess &= dispatcher.registerCommand(
        "back",
        "Undoes an action or a deal by returning to the parent of the current node.",
        [&context]() { return handleBack(context); }
    );

    allSuccess &= dispatcher.registerCommand(
        "root",
        "Returns to the root node.",
        [&context]() { return handleRoot(context); }
    );

    return allSuccess;
}