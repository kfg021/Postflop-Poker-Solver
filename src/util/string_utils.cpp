#include "util/string_utils.hpp"

#include <cctype>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

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

std::string formatBytes(std::size_t bytes) {
    auto getFormattedString = [](float count, const std::string& unit) -> std::string {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << count << " " << unit;
        return ss.str();
    };

    static constexpr std::size_t GB = (1 << 30);
    static constexpr std::size_t MB = (1 << 20);
    static constexpr std::size_t KB = (1 << 10);

    float bytesFloat = static_cast<float>(bytes);

    if (bytes >= GB) {
        return getFormattedString(bytesFloat / GB, "GB");
    }
    else if (bytes >= MB) {
        return getFormattedString(bytesFloat / MB, "MB");
    }
    else if (bytes >= KB) {
        return getFormattedString(bytesFloat / KB, "KB");
    }
    else {
        return getFormattedString(bytesFloat, "bytes");
    }
}