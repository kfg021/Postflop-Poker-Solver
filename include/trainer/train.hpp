#ifndef TRAIN_HPP
#define TRAIN_HPP

#include "game/game_rules.hpp"

#include <optional>
#include <string>

void train(const IGameRules& rules, float targetPercentExploitability, int maxIterations, int exploitabilityCheckFrequency, int numThreads, const std::optional<std::string>& strategyOutputFile);

#endif // TRAIN_HPP