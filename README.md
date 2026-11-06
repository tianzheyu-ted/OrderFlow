# OrderFlow

A C++17 limit order matching engine built around price–time priority, direct order-ID lookup, and pooled intrusive FIFO queues. OrderFlow supports order insertion, cancellation, modification, and full or partial execution, with correctness tests and a synthetic performance benchmark.

## Features

- **Price–time priority:** match against the best available opposing price, then execute orders in arrival order within that price level.
- **Limit-order matching:** execute at the resting order's price, sweep eligible price levels, and rest any unfilled quantity on the book.
- **Order management:** cancel live orders and update their price or remaining quantity.
- **Direct lookup:** map order IDs to pool slots through a preallocated vector, avoiding hash computation in the lookup path.
- **Reusable storage:** embed previous/next slot indices in each order node and recycle inactive slots through a free list.
- **Performance instrumentation:** report throughput and P50, P99, P99.9, and maximum per-submission timer deltas.

## Build and Run

Requirements: a C++17 compiler, Make, and a platform supporting the benchmark's selected timer. There are no third-party library dependencies.

```bash
make all
make run_correctness
make run_stress
```

The default build uses `g++` with `-std=c++17 -O3 -Wall -Wextra -pedantic`. To select another compiler:

```bash
make clean
make CXX=clang++ all
```

Individual targets are also available:

```bash
make correctness
./correctness

make stress
./stress
```

## Usage

```cpp
#include "OrderFlow.hpp"
#include <iostream>

int main() {
    OrderEngine engine;

    // Prices use integer units; 10100 can represent $101.00 in cents.
    const auto restingId = engine.addOrder(10100, 12, Side::Sell);
    engine.addOrder(10100, 5, Side::Buy);

    // One execution: 5 units at 10100. The resting sell has 7 left.
    for (const auto& execution : engine.getTrades()) {
        std::cout << execution.quantity << " @ " << execution.price << '\n';
    }

    engine.updateOrder(restingId, 10100, 4); // Same-price reduction keeps priority.
    engine.cancelOrder(restingId);
}
```

Save the example as `example.cpp` in the repository root and build it with:

```bash
g++ -std=c++17 -O3 -Isrc example.cpp src/OrderFlow.cpp -o example
./example
```

### API

| Method | Behavior |
| --- | --- |
| `addOrder(price, quantity, side)` | Returns a generated order ID; returns `0` for zero price or quantity. An accepted order may fill immediately. |
| `cancelOrder(orderId)` | Removes a live order and returns a success flag. |
| `updateOrder(orderId, newPrice, newQuantity)` | Updates remaining quantity/price and returns a success flag. |
| `getTrades()` | Returns a const reference to accumulated execution records. |
| `clearTrades()` | Clears execution records without clearing the book. |
| `getBestBid()` / `getBestAsk()` | Returns best price and the queue-front order's remaining quantity; returns `{0, 0}` for an empty side. |
| `isBidEmpty()` / `isAskEmpty()` | Checks whether a side has any resting orders. |
| `clear()` | Clears both sides and executions, resets pool bookkeeping, and restarts order IDs at 1. |

Best-price quantity is the first order's quantity, **not aggregated depth** at that price. Invalid-ID handling has a known edge case described below.

### Matching and Update Rules

1. A buy order matches the lowest eligible ask; a sell order matches the highest eligible bid.
2. Orders at the same price execute FIFO, at the resting order's price.
3. The execution quantity is the smaller of the two remaining quantities.
4. Fully filled resting orders are removed; empty price levels are erased.
5. Incoming residual quantity joins the tail of its own price-level queue.
6. A same-price quantity reduction retains priority. Increasing quantity or changing price cancels and resubmits the order under the same ID, allowing immediate matching and resetting queue priority for any remainder.
7. Update quantity means the new **remaining** quantity. Zero-price and zero-quantity updates are rejected; use cancellation to remove an order.

## Implementation

```text
OrderEngine
├── bids / asks: map<price, PriceLevel>
│   └── head / tail slot indices → FIFO order chain
├── orderPool: vector<OrderNode>
│   └── order + prev / next indices + active flag
├── idToIndex: vector<int>
│   └── order ID → pool slot
├── freeHead → reusable inactive slots
└── trades: vector<Trade>
```

Both price maps are ascending: `asks.begin()` identifies the lowest ask and `bids.rbegin()` the highest bid. Each `PriceLevel` stores queue endpoints, while the actual orders live in `orderPool`.

The constructor sizes the ID directory to 1,000,000 entries, reserves 50,000 order nodes, and reserves 100,000 execution records. These are initial storage settings, not hard capacity limits. The ID directory grows when needed, and vectors can allocate again after reserved capacity is exhausted.

The intrusive queue stores links inside `OrderNode`, avoiding a separate standard-list node allocation for each resting order. Removing an order rewires neighboring indices and places the slot on a free list. Slots are reused without shifting other vector elements. Contiguous backing storage comes from the vector pool; linked orders need not occupy adjacent slots.

Order lookup and queue unlinking are O(1). Cancellation also searches a price map, so its overall lookup/removal path is O(log L), where L is the number of price levels. Matching cost depends on the number of orders and levels consumed.

The current source contains the vector-indexed, pooled implementation. A hash-based baseline is not included in the supplied source tree.

## Correctness Tests

`src/test/CorrectnessTests.cpp` contains nine scenario blocks covering:

- Full execution and partial execution of either incoming or resting orders.
- Resting-price execution and same-price FIFO priority.
- Matching across multiple price levels.
- Cancellation of an order within a queue.
- Priority loss after price changes or quantity increases.
- Priority preservation after a same-price quantity reduction.

Tests use standard `assert`; keep assertions enabled when running them. The supplied Makefile does not define `NDEBUG`. This suite covers core scenarios, rather than exhaustive invalid-input, capacity, or randomized differential testing.

## Performance Benchmark

`src/test/StressTests.cpp` pregenerates 1,000,000 submissions using `std::mt19937` with seed `42`:

| Parameter | Value |
| --- | --- |
| Price | Uniform integer distribution, 1–10,000 |
| Quantity | Uniform integer distribution, 1–100 |
| Direction | Random buy or sell, equal probability |
| Warm-up | 10,000 buy/sell pairs, followed by `clear()` |
| Measured workload | Order submissions, including any matching they trigger |
| Output | Orders/sec, P50, P99, P99.9, maximum timer delta |

Input generation and percentile sorting are outside the timed loop. Throughput includes per-order timing and sample-storage overhead. The workload does not benchmark cancellation or modification.

### Timer Units

The executable currently prints `cycles` on every platform, but the actual units depend on the timer:

| Platform path | Timer | Interpretation |
| --- | --- | --- |
| x86-64 | `RDTSC` / `RDTSCP` | Timestamp-counter ticks, not necessarily CPU core cycles |
| AArch64 | `CNTVCT_EL0` | Architectural virtual-counter ticks, not CPU core cycles |
| Other | `steady_clock` | Clock-duration ticks; not automatically nanoseconds |

Counter access must be available on the target platform. Compare measurements using the same machine, compiler, workload, and timer; convert ticks using the relevant counter frequency or clock period before reporting time units.

### Reported Optimization Results

The accompanying project description reports the following comparison:

| Metric | Baseline | Vector-based implementation |
| --- | --- | --- |
| Throughput | 4.21 million orders/sec | 9.82 million orders/sec |
| P99 latency | Reference | 61% lower than baseline |

These are reported project-description figures, not results reproduced by this checkout. The supplied folder does not include the baseline implementation, raw measurements, or the hardware/compiler configuration needed to verify the comparison. A run of `make run_stress` measures only the current implementation. Reproducible comparisons should retain both versions, identical inputs, repeated-run results, and environment details.

## Scope and Current Limitations

- Each engine represents one in-memory order book. Market orders, additional time-in-force policies, persistence, and network connectivity are outside the current implementation.
- Matching and book mutations are single-threaded. Atomic ID generation does not make `OrderEngine` thread-safe.
- The ID vector currently initializes new entries to `0`, although `NULL_INDEX` is `-1`. Cancellation or modification of an unknown ID on a fresh/cleared engine can access an empty pool. Sentinel initialization and slot validation need to be hardened before relying on arbitrary invalid-ID input.
- The ID directory grows with the highest assigned ID, even when few orders remain active. Execution records accumulate until cleared.
- `getBestAskLevel()` and `getBestBidLevel()` require a nonempty side and expose mutable internal state.
- `RingBuffer.hpp` supplies a separate ring-buffer utility, but it is not connected to the active trade-recording path.

## Repository Layout

```text
OrderFlow/
├── Makefile
├── README.md
└── src/
    ├── OrderFlow.hpp           # Public API and book/node structures
    ├── OrderFlow.cpp           # Matching, cancellation, updates, and pool management
    ├── IdGenerator.hpp        # Atomic sequential ID generator
    ├── RingBuffer.hpp         # Standalone utility; not integrated into matching
    └── test/
        ├── CorrectnessTests.cpp
        └── StressTests.cpp
```
