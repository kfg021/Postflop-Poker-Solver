#ifndef FIXED_VECTOR_HPP
#define FIXED_VECTOR_HPP

#include <algorithm>
#include <array>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

template <typename T, std::size_t Capacity>
class FixedVector {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = typename std::array<T, Capacity>::iterator;
    using const_iterator = typename std::array<T, Capacity>::const_iterator;

    FixedVector() : m_buffer{}, m_size(0) {}

    FixedVector(std::initializer_list<T> initList) : m_buffer{}, m_size(initList.size()) {
        assert(m_size <= Capacity);
        std::copy(initList.begin(), initList.end(), m_buffer.begin());
    }

    FixedVector(const std::array<T, Capacity>& buffer) : m_buffer(buffer), m_size(Capacity) {}
    FixedVector(std::array<T, Capacity>&& buffer) : m_buffer(std::move(buffer)), m_size(Capacity) {}

    FixedVector(std::size_t size, const T& data) : m_buffer{}, m_size(size) {
        assert(m_size <= Capacity);
        std::fill(m_buffer.begin(), m_buffer.begin() + m_size, data);
    }
    FixedVector(std::size_t size, T&& data) : m_buffer{}, m_size(size) {
        assert(m_size <= Capacity);
        std::fill(m_buffer.begin(), m_buffer.begin() + m_size, std::move(data));
    }

    iterator begin() {
        return m_buffer.begin();
    }

    iterator end() {
        return m_buffer.begin() + m_size;
    }

    const_iterator begin() const {
        return m_buffer.begin();
    }

    const_iterator end() const {
        return m_buffer.begin() + m_size;
    }

    const_iterator cbegin() const {
        return m_buffer.cbegin();
    }

    const_iterator cend() const {
        return m_buffer.cbegin() + m_size;
    }

    std::size_t size() const {
        return static_cast<std::size_t>(m_size);
    }

    void pushBack(const T& data) {
        assert(m_size < Capacity);
        m_buffer[m_size] = data;
        ++m_size;
    }

    void pushBack(T&& data) {
        assert(m_size < Capacity);
        m_buffer[m_size] = std::move(data);
        ++m_size;
    }

    void popBack() {
        assert(m_size > 0);
        --m_size;
    }

    const T& back() const {
        assert(m_size > 0);
        return m_buffer[m_size - 1];
    }

    const T& operator[](std::size_t index) const {
        assert(index < m_size);
        return m_buffer[index];
    }

    T& operator[](std::size_t index) {
        assert(index < m_size);
        return m_buffer[index];
    }

    bool contains(const T& data) const {
        return std::find(this->begin(), this->end(), data) != this->end();
    }

    auto operator<=>(const FixedVector& rhs) const {
        return std::lexicographical_compare_three_way(
            this->begin(), this->end(),
            rhs.begin(), rhs.end(),
            std::compare_three_way()
        );
    }

private:
    // Save space by storing the size in as small of a type as possible
    using Size =
        std::conditional_t<(Capacity <= std::numeric_limits<uint8_t>::max()), uint8_t,
        std::conditional_t<(Capacity <= std::numeric_limits<uint16_t>::max()), uint16_t,
        std::conditional_t<(Capacity <= std::numeric_limits<uint32_t>::max()), uint32_t,
        std::size_t
        >>>;

    std::array<T, Capacity> m_buffer;
    Size m_size;
};


#endif // FIXED_VECTOR_HPP