#ifndef SCOPED_TIMER_HPP
#define SCOPED_TIMER_HPP

#include <chrono>
#include <string_view>

class ScopedTimer {
public:
    ScopedTimer(std::string_view startMessage, std::string_view endMessage);
    ~ScopedTimer();

private:
    std::chrono::steady_clock::time_point m_startTime;
    std::string_view m_endMessage;
};

#endif // SCOPED_TIMER_HPP