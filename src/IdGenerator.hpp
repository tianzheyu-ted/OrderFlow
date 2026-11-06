#pragma once

#include <cstdint>
#include <atomic>

class IdGenerator {
private: 
    // Use std::atomic to avoid race conditions
    std::atomic<uint64_t> counter;

public:
    IdGenerator() : counter{1} {}

    uint64_t next() noexcept {
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    void reset() noexcept {
        counter.store(1, std::memory_order_relaxed);
    }
};