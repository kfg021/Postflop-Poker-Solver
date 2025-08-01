#ifndef SIZE_STRING_HPP
#define SIZE_STRING_HPP

#include <cstddef>
#include <ostream>
#include <string>

struct DataSize {
    float value;
    std::string units;

    friend std::ostream& operator<<(std::ostream& os, const DataSize& size);
};

DataSize getSizeString(std::size_t bytes);

#endif // SIZE_STRING_HPP