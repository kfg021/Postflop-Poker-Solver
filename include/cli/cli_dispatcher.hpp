#ifndef CLI_DISPATCHER_HPP
#define CLI_DISPATCHER_HPP

#include <functional>
#include <string>
#include <map>
#include <vector>

using HandlerWithoutArgument = std::function<bool()>;
using HandlerWithArgument = std::function<bool(const std::string&)>;

struct Version {
    int major;
    int minor;
    int patch;
};

class CliDispatcher {
public:
    CliDispatcher(const std::string& programName, const Version& version);

    bool registerCommand(const std::string& name, const std::string& description, const HandlerWithoutArgument& handler);
    bool registerCommand(const std::string& name, const std::string& argument, const std::string& description, const HandlerWithArgument& handler);
    void run();

private:
    bool isCommandNameValid(const std::string& name) const;
    bool doIteration();
    bool handleHelp() const;
    bool handleExit();

    std::string m_programName;
    Version m_version;
    bool m_isRunning;
    std::vector<std::string> m_commandOrder;
    std::map<std::string, std::string> m_commandDescriptions;
    std::map<std::string, std::string> m_commandArguments;
    std::map<std::string, HandlerWithoutArgument> m_handlersWithoutArguments;
    std::map<std::string, HandlerWithArgument> m_handlersWithArguments;
};

#endif // CLI_DISPATCHER_HPP