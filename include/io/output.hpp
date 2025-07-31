#ifndef OUTPUT_HPP
#define OUTPUT_HPP

#include <cstddef>
#include <ostream>
#include <string>

struct DataSize {
    float value;
    std::string units;

    friend std::ostream& operator<<(std::ostream& os, const DataSize& size);
};

DataSize getSizeString(std::size_t bytes);

#endif // OUTPUT_HPP