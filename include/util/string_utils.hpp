#ifndef STRING_UTILS_HPP
#define STRING_UTILS_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

std::string trim(const std::string& input);
std::string join(const std::vector<std::string>& inputs, const std::string& connector);
std::vector<std::string> parseTokens(const std::string& input, char delimiter);
std::optional<int> parseInt(const std::string& input);
std::optional<float> parseFloat(const std::string& input);
std::string formatBytes(std::size_t bytes);
std::string formatFixedPoint(double num, int precision);

#endif // STRING_UTILS_HPP 