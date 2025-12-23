#ifndef SOLVER_COMMANDS_HPP
#define SOLVER_COMMANDS_HPP

#include "cli/cli_dispatcher.hpp"
#include "game/game_rules.hpp"
#include "solver/tree.hpp"

#include <memory>

struct SolverContext {
    std::unique_ptr<IGameRules> rules;
    std::unique_ptr<Tree> tree;
    float targetPercentExploitability;
    int maxIterations;
    int exploitabilityCheckFrequency;
    int numThreads;
};

bool registerAllCommands(CliDispatcher& dispatcher, SolverContext& context);

#endif // SOLVER_COMMANDS_HPP