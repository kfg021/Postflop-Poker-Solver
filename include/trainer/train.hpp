#ifndef TRAIN_HPP
#define TRAIN_HPP

#include "game/game_rules.hpp"

#include <string>

void train(const IGameRules& rules, int iterations, int printFrequency, const std::string& strategyOutputFile);

#endif // TRAIN_HPP