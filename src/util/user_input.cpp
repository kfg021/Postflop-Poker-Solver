#include "util/user_input.hpp"

#include <cctype>
#include <exception>
#include <optional>
#include <string>
#include <vector>

namespace {
std::string trim(const std::string& input) {
    int inputSize = input.size();

    int start = 0;
    while (start < inputSize && std::isspace(input[start])) {
        ++start;
    }

    int end = inputSize - 1;
    while (end >= 0 && std::isspace(input[end])) {
        --end;
    }

    if (end < start) {
        return "";
    }

    int outputLength = end - start + 1;
    return input.substr(start, outputLength);
}
} // namespace

std::vector<std::string> parseTokens(const std::string& input, char delimiter) {
    std::vector<std::string> tokens;

    auto insertToken = [&input, &tokens](int start, int end) {
        int tokenSize = end - start + 1;
        if (tokenSize > 0) {
            std::string trimmed = trim(input.substr(start, tokenSize));
            if (!trimmed.empty()) {
                tokens.push_back(trimmed);
            }
        }
    };


    int inputSize = input.size();
    int nextTokenStart = 0;
    for (int i = 0; i < inputSize; ++i) {
        if (input[i] == delimiter) {
            int nextTokenEnd = i - 1;
            insertToken(nextTokenStart, nextTokenEnd);
            nextTokenStart = i + 1;
        }
    }
    insertToken(nextTokenStart, inputSize - 1);

    return tokens;
}

std::optional<int> parseInt(const std::string& input) {
    try {
        return std::stoi(input);
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<float> parseFloat(const std::string& input) {
    try {
        return std::stof(input);
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}