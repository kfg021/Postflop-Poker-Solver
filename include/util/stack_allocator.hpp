#ifndef STACK_ALLOCATOR_HPP
#define STACK_ALLOCATOR_HPP

#include "util/fixed_vector.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

template<typename T>
class StackAllocator {
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

    std::span<T> allocate(int thread, int size) {
        assert(thread < MaxNumThreads);
        assert(m_stackPointers[thread] + size <= m_stacks[thread].size());

        auto start = m_stacks[thread].begin() + m_stackPointers[thread];
        auto end = start + size;
        m_stackPointers[thread] += size;

        m_maximumStackUsage[thread] = std::max(m_maximumStackUsage[thread], m_stackPointers[thread]);

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
class StackVector {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = typename std::span<T>::iterator;

    StackVector(StackAllocator<T>& allocator, int allocatingThread, std::size_t size) : m_allocatingThread{ allocatingThread }, m_allocator{ allocator }, m_data{ allocator.allocate(allocatingThread, size) } {}

    ~StackVector() {
        m_allocator.deallocate(m_allocatingThread, m_data);
    }

    // StackVectors must be allocated on the stack and they are tied to a specific scope
    StackVector(const StackVector&) = delete;
    StackVector& operator=(const StackVector&) = delete;
    StackVector(StackVector&&) = delete;
    StackVector& operator=(StackVector&&) = delete;
    void* operator new(std::size_t) = delete;
    void* operator new[](std::size_t) = delete;
    void operator delete(void*) = delete;
    void operator delete[](void*) = delete;

    iterator begin() {
        return m_data.begin();
    }

    iterator end() {
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

private:
    StackAllocator<T>& m_allocator;
    int m_allocatingThread;
    std::span<T> m_data;
};

#endif // STACK_ALLOCATOR_HPP