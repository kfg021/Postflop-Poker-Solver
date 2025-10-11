#ifndef OUTPUT_HPP
#define OUTPUT_HPP

#include "game/game_rules.hpp"
#include "solver/tree.hpp"

#include <string>

void outputStrategyToJSON(const IGameRules& rules, Tree& tree, const std::string& filePath);

#endif // OUTPUT_HPP