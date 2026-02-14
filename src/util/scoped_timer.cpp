#include "util/scoped_timer.hpp"

#include "util/string_utils.hpp"

#include <chrono>
#include <iostream>
#include <string_view>

ScopedTimer::ScopedTimer(std::string_view startMessage, std::string_view endMessage) : m_endMessage{ endMessage } {
    if (!startMessage.empty()) {
        std::cout << startMessage << "\n" << std::flush;
    }

    m_startTime = std::chrono::steady_clock::now();
}

ScopedTimer::~ScopedTimer() {
    auto endTime = std::chrono::steady_clock::now();
    double secondsElapsed = std::chrono::duration<double>(endTime - m_startTime).count();
    std::cout << m_endMessage << " in " << formatFixedPoint(secondsElapsed, 3) << "s.\n";
}