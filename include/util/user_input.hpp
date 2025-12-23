#include <optional>
#include <string>
#include <vector>

std::vector<std::string> parseTokens(const std::string& input, char delimiter);
std::optional<int> parseInt(const std::string& input);
std::optional<float> parseFloat(const std::string& input);