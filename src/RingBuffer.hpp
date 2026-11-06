#pragma once

#include <vector>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <stdexcept>

constexpr size_t CACHE_LINE_SIZE = 64;

template <typename T>
class RingBuffer 
{
public:
    RingBuffer(size_t capacity) {
        if ((capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument("Capacity must be a power of two");
        }

        buffer.resize(capacity);
        mask = capacity - 1;
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
    }

    // PRODUCER adds data
    bool push(const T& item) {
        const size_t currentTail = tail.load(std::memory_order_relaxed);
        const size_t nextTail = (currentTail + 1) & mask;

        const size_t currentHead = head.load(std::memory_order_acquire);
        if (nextTail == currentHead) {
            return false; // buffer is full
        }

        buffer[currentTail] = item;
        tail.store(nextTail, std::memory_order_release);
        return true;
    }

    bool pop(T& output) {
        const size_t currentHead = head.load(std::memory_order_relaxed);

        const size_t currentTail = tail.load(std::memory_order_acquire);
        if (currentHead == currentTail) {
            return false; // buffer is empty
        }

        output = buffer[currentHead];

        const size_t nextHead = (currentHead + 1) & mask;
        head.store(nextHead, std::memory_order_release);
        return true;
    }

private:
    std::vector<T> buffer;
    size_t mask;

    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head; // readIndex
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail; // writeIndex
};