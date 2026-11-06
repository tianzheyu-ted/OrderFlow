#include "../OrderFlow.hpp"

#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#endif


// Simple struct to hold test data
struct OrderData {
    uint64_t price;
    uint64_t quantity;
    Side side;
};

inline uint64_t startTimer() {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_lfence();
    return __rdtsc();
#elif defined(__aarch64__)
    uint64_t val;
    asm volatile("isb; mrs %0, cntvct_el0" : "=r"(val));
    return val;
#else
    return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
}

inline uint64_t stopTimer() {
#if defined(__x86_64__) || defined(_M_X64)
    unsigned int aux;
    return __rdtscp(&aux);
#elif defined(__aarch64__)
    uint64_t val;
    asm volatile("mrs %0, cntvct_el0; isb" : "=r"(val));
    return val;
#else
    return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
}

// Configuration
const int NUM_ORDERS = 1'000'000; // 1 Million orders

void runStressTest() {
    std::cout << "Generating " << NUM_ORDERS << " orders in memory..." << std::endl;

    std::vector<OrderData> orders;
    orders.reserve(NUM_ORDERS);
    
    std::mt19937 gen(42);
    std::uniform_int_distribution<uint64_t> priceDist(1, 10000);
    std::uniform_int_distribution<uint64_t> quantDist(1, 100);
    std::uniform_int_distribution<int> sideDist(0, 1);

    for (int i = 0; i < NUM_ORDERS; ++i) {
        Side side = (sideDist(gen) == 0) ? Side::Buy : Side::Sell;
        orders.push_back({priceDist(gen), quantDist(gen), side});
    }

    OrderEngine book;

    std::cout << "Warming up..." << std::endl;
    // Run 10k ops to heat up CPU cache and trigger OS page faults
    for(int i=0; i<10000; ++i) {
        book.addOrder(100, 10, Side::Buy);
        book.addOrder(100, 10, Side::Sell);
    }
    book.clear();

    std::cout << "Starting Benchmark..." << std::endl;

    
    // 1. Create a vector to store the cycle of EVERY order
    std::vector<uint64_t> cycles;
    cycles.resize(NUM_ORDERS);

    // Pointers for faster access (avoids vector bounds checks in debug modes)
    uint64_t* cycle_ptr = cycles.data();
    const OrderData* order_ptr = orders.data();

    auto global_start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_ORDERS; ++i) {
        // A. Start local clock
        uint64_t start = startTimer();

        book.addOrder(order_ptr[i].price, order_ptr[i].quantity, order_ptr[i].side);

        // B. Stop local clock
        uint64_t end = stopTimer();

        // C. Calculate cycles and store it
        cycle_ptr[i] = end - start;
    }

    // --- Analysis Phase ---

    auto global_end = std::chrono::high_resolution_clock::now(); 

    // 2. Sort the latencies to find percentiles
    std::sort(cycles.begin(), cycles.end());

    // 3. Calculate Percentile Indices
    size_t p50_idx = NUM_ORDERS * 0.50;
    size_t p99_idx = NUM_ORDERS * 0.99;
    size_t p999_idx = NUM_ORDERS * 0.999;
    size_t worst_idx = NUM_ORDERS - 1;

    double totalTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(global_end - global_start).count();
    double opsPerSec = (double)NUM_ORDERS / (totalTimeNs / 1e9);

    std::cout << "--------------------------------" << std::endl;
    std::cout << "Throughput:    " << opsPerSec << " orders/sec" << std::endl;
    std::cout << "--------------------------------" << std::endl;
    std::cout << "P50 (Median): " << cycles[p50_idx] << " cycles" << std::endl;
    std::cout << "P99 (Tail):   " << cycles[p99_idx] << " cycles" << std::endl;
    std::cout << "P99.9:        " << cycles[p999_idx] << " cycles" << std::endl;
    std::cout << "Max (Worst):  " << cycles[worst_idx] << " cycles" << std::endl;
    std::cout << "--------------------------------" << std::endl;
}

int main() {
    runStressTest();
    return 0;
}