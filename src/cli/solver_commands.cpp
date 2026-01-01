#include "cli/solver_commands.hpp"

#include "cli/cli_dispatcher.hpp"
#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/holdem/holdem_parser.hpp"
#include "game/holdem/holdem.hpp"
#include "game/kuhn_poker.hpp"
#include "game/leduc_poker.hpp"
#include "solver/cfr.hpp"
#include "solver/tree.hpp"
#include "util/stack_allocator.hpp"
#include "util/string_utils.hpp"

#include <cassert>
#include <iomanip>
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
        std::cout << "Tree skeleton not yet built, building...\n" << std::flush;
        context.tree->buildTreeSkeleton(*context.rules);
        std::cout << "Finished building tree skeleton.\n\n";
    }
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
        catch (const YAML::Exception& e) {
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
    for (Player player : {Player::P0, Player::P1}) {
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
    static constexpr int DefaultNumThreads = 6;
    #else
    static constexpr int DefaultNumThreads = 1;
    #endif
    loadOptionalIntWithBounds(context.numThreads, input, { "solver", "threads" }, DefaultNumThreads, 1, 64);

    // Load target exploitability
    loadOptionalField(context.targetPercentExploitability, input, { "solver", "target-exploitability" }, 0.3f);

    // Load max iterations
    loadOptionalIntWithBounds(context.maxIterations, input, { "solver", "max-iterations" }, 1000, 1, std::nullopt);

    // Load exploitability check frequency
    loadOptionalIntWithBounds(context.exploitabilityCheckFrequency, input, { "solver", "exploitability-check-frequency" }, 10, 1, std::nullopt);

    std::cout << "Successfully loaded Holdem settings.\n\n";

    std::cout << "Building Holdem lookup tables...\n";
    context.rules = std::make_unique<Holdem>(settings);
    std::cout << "Successfully built lookup tables.\n";

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

    // TODO: Print out default settings
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
        #ifdef _OPENMP
        .numThreads = 6
        #else
        .numThreads = 1
        #endif
    };

    // TODO: Print out default settings
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
                std::cout << "Finished iteration " << iteration << ". Exploitability: " << std::fixed << std::setprecision(5) << exploitability << " (" << exploitabilityPercent << "%)\n";
                if (exploitabilityPercent <= context.targetPercentExploitability) {
                    return CfrResult{ exploitability, iteration };
                }
            }
        }

        return std::nullopt;
    };

    if (!isContextValid(context)) {
        printInvalidContextError();
        return false;
    }

    buildTreeSkeletonIfNeeded(context);
    context.tree->initCfrVectors();
    std::optional<CfrResult> resultOption;

    #ifdef _OPENMP
    omp_set_num_threads(context.numThreads);
    StackAllocator<float> allocator(context.numThreads);

    #pragma omp parallel
    {
        #pragma omp single
        {
            std::cout << "Starting training in parallel with " << omp_get_num_threads() << " threads. Target exploitability: "
                << std::fixed << std::setprecision(5) << context.targetPercentExploitability
                << "% Maximum iterations: " << context.maxIterations << "\n" << std::flush;

            resultOption = runCfr(allocator);
        }
    }
    #else
    context.numThreads = 1;
    StackAllocator<float> allocator(context.numThreads);
    std::cout << "Starting training in single-threaded mode. Target exploitability: "
        << std::fixed << std::setprecision(5) << context.targetPercentExploitability
        << "% Maximum iterations: " << context.maxIterations << "\n" << std::flush;

    resultOption = runCfr(allocator);
    #endif

    std::cout << "Finished training.\n";

    if (resultOption) {
        std::cout << "Target exploitability percentage reached after iteration " << resultOption->iteration << ".\n\n";
    }
    else {
        std::cout << "Target exploitability percentage not reached.\n\n";
    }

    std::cout << "Calculating expected value of final strategy...\n" << std::flush;
    float player0ExpectedValue = expectedValue(Player::P0, *context.rules, *context.tree, allocator);
    float player1ExpectedValue = expectedValue(Player::P1, *context.rules, *context.tree, allocator);
    std::cout << "Player 0 expected value: " << std::fixed << std::setprecision(5) << player0ExpectedValue << "\n";
    std::cout << "Player 1 expected value: " << std::fixed << std::setprecision(5) << player1ExpectedValue << "\n\n";

    std::cout << "Calculating exploitability of final strategy...\n" << std::flush;
    float exploitability = resultOption ? resultOption->exploitability : calculateExploitabilityFast(*context.rules, *context.tree, allocator);
    float exploitabilityPercent = (exploitability / static_cast<float>(getStartingPot())) * 100.0f;
    std::cout << "Exploitability: " << std::fixed << std::setprecision(5) << exploitability << " (" << exploitabilityPercent << "%)\n\n";

    std::cout << "Maximum stack allocator memory usage per thread: ";
    auto stackUsages = allocator.getMaximumStackUsage();
    for (int i = 0; i < context.numThreads; ++i) {
        std::cout << formatBytes(stackUsages[i]);
        if (i < context.numThreads - 1) std::cout << ", ";
    }
    std::cout << "\n";

    return true;
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
        "tree-size",
        "Provides an estimate of the size of the tree. Game settings must be loaded first.",
        [&context]() { return handleEstimateTreeSize(context); }
    );

    allSuccess &= dispatcher.registerCommand(
        "solve",
        "Solves the game tree using Discounted CFR. It is recommended to first run \"tree-size\" to ensure that the tree fits in RAM.",
        [&context]() { return handleSolve(context); }
    );

    // TODO: Add node exploring

    return allSuccess;
}