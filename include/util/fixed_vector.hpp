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

    constexpr FixedVector() : m_buffer{}, m_size{ 0 } {}

    constexpr FixedVector(std::initializer_list<T> initList) : m_buffer{}, m_size{ static_cast<Size>(initList.size()) } {
        assert(initList.size() <= Capacity);
        std::copy(initList.begin(), initList.end(), m_buffer.begin());
    }

    constexpr FixedVector(const std::array<T, Capacity>& buffer) : m_buffer{ buffer }, m_size{ Capacity } {}
    constexpr FixedVector(std::array<T, Capacity>&& buffer) : m_buffer{ std::move(buffer) }, m_size{ Capacity } {}

    constexpr FixedVector(std::size_t size, const T& data) : m_buffer{}, m_size{ static_cast<Size>(size) } {
        assert(size <= Capacity);
        std::fill(m_buffer.begin(), m_buffer.begin() + m_size, data);
    }
    constexpr FixedVector(std::size_t size, T&& data) : m_buffer{}, m_size{ static_cast<Size>(size) } {
        assert(size <= Capacity);
        std::fill(m_buffer.begin(), m_buffer.begin() + m_size, std::move(data));
    }
    constexpr FixedVector(std::size_t size) : m_buffer{}, m_size{ static_cast<Size>(size) } {
        assert(size <= Capacity);
    }

    constexpr iterator begin() {
        return m_buffer.begin();
    }

    constexpr iterator end() {
        return m_buffer.begin() + m_size;
    }

    constexpr const_iterator begin() const {
        return m_buffer.begin();
    }

    constexpr const_iterator end() const {
        return m_buffer.begin() + m_size;
    }

    constexpr const_iterator cbegin() const {
        return m_buffer.cbegin();
    }

    constexpr const_iterator cend() const {
        return m_buffer.cbegin() + m_size;
    }

    constexpr std::size_t size() const {
        return static_cast<std::size_t>(m_size);
    }

    constexpr void pushBack(const T& data) {
        assert(m_size < Capacity);
        m_buffer[m_size] = data;
        ++m_size;
    }

    constexpr void pushBack(T&& data) {
        assert(m_size < Capacity);
        m_buffer[m_size] = std::move(data);
        ++m_size;
    }

    constexpr void popBack() {
        assert(m_size > 0);
        --m_size;
    }

    constexpr const T& back() const {
        assert(m_size > 0);
        return m_buffer[m_size - 1];
    }

    constexpr const T& operator[](std::size_t index) const {
        assert(index < m_size);
        return m_buffer[index];
    }

    constexpr T& operator[](std::size_t index) {
        assert(index < m_size);
        return m_buffer[index];
    }

    constexpr bool contains(const T& data) const {
        return std::find(this->begin(), this->end(), data) != this->end();
    }

    constexpr auto operator<=>(const FixedVector& rhs) const {
        return std::lexicographical_compare_three_way(
            this->begin(), this->end(),
            rhs.begin(), rhs.end(),
            std::compare_three_way()
        );
    }

private:
    // Save space by storing the size in as small of a type as possible
    using Size =
        std::conditional_t<(Capacity <= std::numeric_limits<std::uint8_t>::max()), std::uint8_t,
        std::conditional_t<(Capacity <= std::numeric_limits<std::uint16_t>::max()), std::uint16_t,
        std::conditional_t<(Capacity <= std::numeric_limits<std::uint32_t>::max()), std::uint32_t,
        std::size_t
        >>>;

    std::array<T, Capacity> m_buffer;
    Size m_size;
};


#endif // FIXED_VECTOR_HPP