#include "util/size_string.hpp"

#include <cstddef>
#include <iomanip>
#include <ostream>

std::ostream& operator<<(std::ostream& os, const DataSize& size) {
    os << std::fixed << std::setprecision(2) << size.value << ' ' << size.units;
    return os;
}

DataSize getSizeString(std::size_t bytes) {
    static constexpr std::size_t GB = (1 << 30);
    static constexpr std::size_t MB = (1 << 20);
    static constexpr std::size_t KB = (1 << 10);

    if (bytes >= GB) {
        return { static_cast<float>(bytes) / GB, "GB" };
    }
    else if (bytes >= MB) {
        return { static_cast<float>(bytes) / MB, "MB" };
    }
    else if (bytes >= KB) {
        return { static_cast<float>(bytes) / KB, "KB" };
    }
    else {
        return { static_cast<float>(bytes), "bytes" };
    }
}