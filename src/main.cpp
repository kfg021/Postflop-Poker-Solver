#include "cli/cli_dispatcher.hpp"
#include "cli/solver_commands.hpp"

int main() {
    CliDispatcher dispatcher("PostflopSolver", Version{ .major = 1, .minor = 0, .patch = 0 });
    SolverContext context;
    bool success = registerAllCommands(dispatcher, context);
    assert(success);

    dispatcher.run();

    return 0;
}