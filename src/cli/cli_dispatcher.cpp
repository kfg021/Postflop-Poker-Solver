#include "cli/cli_dispatcher.hpp"

#include "util/user_input.hpp"

#include <cctype>
#include <iostream>
#include <map>
#include <string>

CliDispatcher::CliDispatcher(const std::string& programName, const Version& version) : m_programName{ programName }, m_version{ version }, m_isRunning{ false } {
    // Register help and exit by default
    registerCommand("help", "Prints this help page.", [this]() { return handleHelp(); });
    registerCommand("exit", "Exits the program.", [this]() { return handleExit(); });
}

bool CliDispatcher::isCommandNameValid(const std::string& name) const {
    // Command name should be visible characters only
    for (char c : name) {
        if (!std::isgraph(c)) {
            return false;
        }
    }

    // Command should not already be registered
    if (m_commandDescriptions.find(name) != m_commandDescriptions.end()) {
        return false;
    }

    return true;
}

bool CliDispatcher::registerCommand(const std::string& name, const std::string& description, const HandlerWithoutArgument& handler) {
    if (!isCommandNameValid(name)) {
        return false;
    }

    m_commandOrder.push_back(name);
    m_commandDescriptions.insert({ name, description });
    m_handlersWithoutArguments.insert({ name, handler });
    return true;
}

bool CliDispatcher::registerCommand(const std::string& name, const std::string& argument, const std::string& description, const HandlerWithArgument& handler) {
    if (!isCommandNameValid(name)) {
        return false;
    }

    m_commandOrder.push_back(name);
    m_commandDescriptions.insert({ name, description });
    m_commandArguments.insert({ name, argument });
    m_handlersWithArguments.insert({ name, handler });
    return true;
}

void CliDispatcher::run() {
    m_isRunning = true;

    std::cout << m_programName << " " << m_version.major << "." << m_version.minor << "." << m_version.patch << "\n";
    std::cout << "Type \"help\" for more information.\n";
    while (m_isRunning) {
        doIteration();
    }
}

bool CliDispatcher::doIteration() {
    std::cout << "> ";
    std::string userInput;
    std::getline(std::cin, userInput);
    std::vector<std::string> tokens = parseTokens(userInput, ' ');
    if (tokens.empty()) {
        return false;
    }

    const std::string& commandName = tokens[0];
    int numArguments = static_cast<int>(tokens.size()) - 1;

    if (m_handlersWithoutArguments.find(commandName) != m_handlersWithoutArguments.end()) {
        if (numArguments != 0) {
            std::cerr << "Error: Incorrect number of arguments provided for " << commandName << ": Expected 0, got " << numArguments << "\n";
            return false;
        }

        return m_handlersWithoutArguments[commandName]();
    }
    else if (m_handlersWithArguments.find(commandName) != m_handlersWithArguments.end()) {
        if (numArguments != 1) {
            std::cerr << "Error: Incorrect number of arguments provided for " << commandName << ": Expected 1, got " << numArguments << "\n";
            return false;
        }

        const std::string& argument = tokens[1];
        return m_handlersWithArguments[commandName](argument);
    }
    else {
        std::cerr << "Error: Unknown command: " << commandName << "\n";
        return false;
    }
}

bool CliDispatcher::handleHelp() const {
    std::cout << m_programName << " options:\n";
    for (const std::string& name : m_commandOrder) {
        std::cout << name;

        if (m_commandArguments.find(name) != m_commandArguments.end()) {
            std::cout << " <" << m_commandArguments.at(name) << "> ";
        }

        std::cout << ": " << m_commandDescriptions.at(name) << "\n";
    }
    return true;
}

bool CliDispatcher::handleExit() {
    m_isRunning = false;
    return true;
}