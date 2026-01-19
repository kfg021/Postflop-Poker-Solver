#ifndef SOLVER_COMMANDS_HPP
#define SOLVER_COMMANDS_HPP

#include "cli/cli_dispatcher.hpp"
#include "game/game_rules.hpp"
#include "solver/tree.hpp"

#include <cstddef>
#include <memory>
#include <vector>

struct SolverContext {
    std::unique_ptr<IGameRules> rules;
    std::unique_ptr<Tree> tree;
    float targetPercentExploitability;
    int maxIterations;
    int exploitabilityCheckFrequency;
    int numThreads;
    std::vector<std::size_t> nodePath;
};

bool registerAllCommands(CliDispatcher& dispatcher, SolverContext& context);

#endif // SOLVER_COMMANDS_HPP