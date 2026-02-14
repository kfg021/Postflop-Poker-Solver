#ifndef STACK_ALLOCATOR_HPP
#define STACK_ALLOCATOR_HPP

#include "util/fixed_vector.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

// TODO: Remove template and allow all trivally copyable types at once
template<typename T>
class StackAllocator {
    static_assert(std::is_trivially_copyable_v<T>);
public:
    static constexpr int MaxNumThreads = 64;

    StackAllocator(int numThreads) : m_stackPointers(numThreads, 0), m_stacks(numThreads, std::vector<T>(StackBytesPerThread / sizeof(T))), m_maximumStackUsage(numThreads, 0) {
        assert(numThreads <= MaxNumThreads);
    }

    bool isEmpty() const {
        for (std::size_t stackPointer : m_stackPointers) {
            if (stackPointer > 0) return false;
        }
        return true;
    }

    std::span<T> allocate(int thread, std::size_t size) {
        assert(thread < MaxNumThreads);
        assert(m_stackPointers[thread] + size <= m_stacks[thread].size());

        auto start = m_stacks[thread].begin() + m_stackPointers[thread];
        auto end = start + size;
        m_stackPointers[thread] += size;

        std::size_t currentStackUsage = m_stackPointers[thread] * sizeof(T);
        m_maximumStackUsage[thread] = std::max(m_maximumStackUsage[thread], currentStackUsage);

        return { start, end };
    }

    void deallocate(int thread, std::span<T> data) {
        assert(thread < MaxNumThreads);
        assert(m_stackPointers[thread] - data.size() >= 0);

        T* expectedTopOfStack = std::to_address(data.end());
        T* currentTopOfStack = std::to_address(m_stacks[thread].begin() + m_stackPointers[thread]);
        assert(expectedTopOfStack == currentTopOfStack);

        m_stackPointers[thread] -= data.size();
    }

    FixedVector<std::size_t, MaxNumThreads> getMaximumStackUsage() const {
        return m_maximumStackUsage;
    }

private:
    static constexpr int KB = (1 << 10);
    static constexpr int StackBytesPerThread = 512 * KB; // TODO: Maybe dynamically choose this based on tree size
    FixedVector<std::size_t, MaxNumThreads> m_stackPointers;
    FixedVector<std::vector<T>, MaxNumThreads> m_stacks;
    FixedVector<std::size_t, MaxNumThreads> m_maximumStackUsage;
};

template <typename T>
class ScopedVector {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = typename std::span<T>::iterator;

    ScopedVector(StackAllocator<T>& allocator, int allocatingThread, std::size_t size) : m_allocator{ allocator }, m_allocatingThread{ allocatingThread }, m_data{ allocator.allocate(allocatingThread, size) } {}

    ~ScopedVector() {
        m_allocator.deallocate(m_allocatingThread, m_data);
    }

    // ScopedVectors must be allocated on the stack and they are tied to a specific scope
    ScopedVector(const ScopedVector&) = delete;
    ScopedVector& operator=(const ScopedVector&) = delete;
    ScopedVector(ScopedVector&&) = delete;
    ScopedVector& operator=(ScopedVector&&) = delete;
    void* operator new(std::size_t) = delete;
    void* operator new[](std::size_t) = delete;
    void operator delete(void*) = delete;
    void operator delete[](void*) = delete;

    iterator begin() const {
        return m_data.begin();
    }

    iterator end() const {
        return m_data.end();
    }

    std::size_t size() const {
        return m_data.size();
    }

    const T& operator[](std::size_t index) const {
        assert(index < m_data.size());
        return m_data[index];
    }

    T& operator[](std::size_t index) {
        assert(index < m_data.size());
        return m_data[index];
    }

    std::span<const T> getData() const {
        return m_data;
    }

    std::span<T> getData() {
        return m_data;
    }

private:
    StackAllocator<T>& m_allocator;
    int m_allocatingThread;
    std::span<T> m_data;
};

#endif // STACK_ALLOCATOR_HPP