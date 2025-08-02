#ifndef TRAIN_HPP
#define TRAIN_HPP

#include "game/game_rules.hpp"

#include <array>
#include <cstdint>
#include <string>

void train(const IGameRules& rules, const std::array<std::uint16_t, 2>& rangeSizes, int iterations, const std::string& strategyOutputFile);

#endif // TRAIN_HPP