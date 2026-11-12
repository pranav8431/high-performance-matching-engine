# lob: a low-latency limit order book and matching engine in C++20

A single-instrument, price-time-priority matching engine built the way
exchange and HFT systems build them: integer ticks, index-based intrusive data
structures, a custom hash index, no allocation on the hot path, and a test
suite that proves the fast implementation agrees with a naive reference model
on millions of random operations.

Header-only, no dependencies, builds with CMake or a one-file Makefile.

## Highlights

* **9-10 million events/s** on a realistic mixed flow (adds, cancels, modifies,
  IOC and market orders), **~100 ns per event**, single thread, laptop CPU.
* **O(1) add / cancel / reduce / fill** via a slab-allocated order pool
  addressed by 32-bit slot indices, an open-addressing `OrderId -> slot` hash
  table with backward-shift deletion, and per-tick price levels with a bitmap
  for best-price search.
* **Order types:** limit GTC, IOC, FOK, market; cancel; cancel/replace with
  exchange-style priority rules (size reductions keep queue position).
* **Differential fuzz testing** against a `std::map` / `std::deque` reference
  implementation, plus unit tests, ASan/UBSan, and CI on GCC and Clang.
* **Tail-latency tooling:** per-event `rdtsc` timing into a 1 ns histogram
  with p50 / p99 / p99.9 / p99.99, and a replay tool for event files.
* **Measured, explained trade-offs:** see [docs/DESIGN.md](docs/DESIGN.md) for
  the data layout, a rejected hash-table experiment with numbers, and why the
  first latency measurements were wrong.

## Architecture

```
                 MarketEvent (ADD / CANCEL / MODIFY / IOC / FOK / MARKET)
                                      │
                  replay.cpp ─────────┤──────── WorkloadGenerator
                  EventParser         ▼
                              ┌───────────────┐
                              │  MarketRelay  │  dispatch + statistics
                              └───────┬───────┘
                                      ▼
                              ┌───────────────┐   on_trade(Trade) callback
                              │ MatchingEngine│──────────────────────────▶
                              │ price-time,   │
                              │ GTC/IOC/FOK/  │
                              │ market, modify│
                              └───────┬───────┘
                                      ▼
   ┌──────────────────────────── OrderBook ────────────────────────────┐
   │                                                                   │
   │  OrderPool          OrderIndex             BookSide (bids, asks)  │
   │  vector<OrderNode>  open addressing        vector<PriceLevel>     │
   │  32 B nodes,        id -> slot, load<=0.5  32 B per tick          │
   │  free list,         Fibonacci hash,        intrusive FIFO per     │
   │  32-bit links       backward-shift delete  level, bitmap of       │
   │                                            non-empty levels       │
   └───────────────────────────────────────────────────────────────────┘
```

* **OrderNode** is 32 bytes: id, price, remaining quantity, side, and
  `prev`/`next` slot indices. Indices stay valid when the pool grows; pointers
  or `std::list` iterators would not allow contiguous storage.
* **BookSide** maps prices so the best price is always index 0 on both sides
  (`max_price - price` for bids, `price - min_price` for asks). "Find the
  best level" is "find the lowest set bit" with `std::countr_zero`.
* **MatchingEngine** validates (duplicate id, zero quantity, price range,
  FOK liquidity) before touching the book, so a rejected order never leaves a
  partial fill behind. Trades go to a callback; no vector is allocated.

## Benchmarks

Hardware: AMD Ryzen 7 4800H (laptop), Linux 7.2, GCC 13.3, `-O3`, single
thread, no core isolation. Workloads are generated before the timer starts, so
only engine time is measured. Numbers are from two consecutive runs and are
given as a range where they differed.

### Before and after, same machine, same day

The previous implementation used `std::map<Price, PriceLevel>`,
`std::list<Order>` per level and `std::unordered_map` for the id index
(commit `589fe41`). Both versions were rebuilt and run back to back.

| workload                                    | before       | after          | change |
|---------------------------------------------|-------------:|---------------:|-------:|
| 50M crossing limit orders, price U[95,105]  | 4.94 M/s     | 7.1 - 8.0 M/s  | ~1.5x  |
| 1M random cancels on a 10M-order book       | 2.41 M/s     | 7.69 M/s       | 3.2x   |

### Throughput on the realistic flow

58% passive limit adds, 6% crossing limits, 2% IOC, 1% market, 26% cancels,
7% modifies, prices clustered near a drifting mid, 50M events:

| build                    | events/s   | ns/event |
|--------------------------|-----------:|---------:|
| `-O3`                    | 9.2 - 9.9 M| 101 - 109|
| `-O3 -march=native`      | 10.45 M    | 96       |

That run generated 22.2M trades and finished with 2.95M resting orders across
240 price levels.

### Per-event latency

Realistic flow, 20M events (1M warm-up, 19M measured), each event timed with
fenced `rdtsc`/`rdtscp`. The 20 ns timer overhead is included in every sample.
Index and pool sized for the run (`expected_orders = 2M`), so no rehash occurs.

| operation      | samples | mean  | p50   | p90   | p99   | p99.9   | p99.99  | max     |
|----------------|--------:|------:|------:|------:|------:|--------:|--------:|--------:|
| add (passive)  | 8.2M    | 186 ns| 150 ns| 220 ns| 661 ns| 2.8 us  | 13.7 us | 726 us  |
| add (trading)  | 4.5M    | 238 ns| 180 ns| 381 ns| 792 ns| 1.3 us  | 14.8 us | 835 us  |
| cancel         | 4.9M    |  94 ns|  80 ns| 130 ns| 331 ns| 0.7 us  |  9.1 us | 774 us  |
| modify         | 1.3M    | 143 ns| 120 ns| 240 ns| 621 ns| 1.0 us  | 13.2 us | 597 us  |
| all events     | 19.0M   | 172 ns| 140 ns| 250 ns| 671 ns| 1.2 us  | 13.8 us | 835 us  |

The ~10 us p99.99 and sub-millisecond maxima are consistent with interrupts
and scheduler preemption on a non-isolated core; the engine itself has no
operation whose cost scales with book size.

**Two measurement lessons the numbers taught:**

1. The first version of this benchmark used `std::chrono::steady_clock` and
   reported a flat 1.4 us median for every operation. The machine's
   clocksource is `hpet`, so each clock read was a 1.2 us system call. Always
   measure the timer before trusting it.
2. With the index under-provisioned (`expected_orders = 1M`), every percentile
   through p99.99 is unchanged but the maximum jumps to **74 ms**: one hash
   table rehash copying 2M entries plus a pool reallocation, triggered when
   resting orders crossed 1M. Throughput moves by less than 1%. Tail latency
   has to be observed directly; it cannot be inferred from averages.

### Order index vs `std::unordered_map`

10M mostly-sequential ids, insert all, 10M lookups at 50% hit rate, erase all:

| operation       | `lob::OrderIndex` | `std::unordered_map` (reserved) |
|-----------------|------------------:|--------------------------------:|
| insert          | 27.6 ns           | 41.0 ns                         |
| find            | 17.3 ns           | 74.6 ns                         |
| erase           | 38.6 ns           | 17.0 ns                         |

`unordered_map` wins the erase pass only because libstdc++ hashes integers by
identity and the keys are erased in insertion order, which is cache-sequential
for it and randomly scattered for the Fibonacci-hashed table. A
locality-preserving variant was measured and rejected; see
[docs/DESIGN.md](docs/DESIGN.md#experiment-exploiting-monotonic-order-ids-rejected).

### Caveats

These are synthetic single-threaded microbenchmarks on a laptop with a
desktop session running. They show the relative cost of design choices, not
exchange-grade figures. There is no network, no serialization and no
multi-symbol routing in the measured path.

## Build and run

Requires a C++20 compiler (GCC 11+, Clang 14+). No other dependencies.

```bash
# CMake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release     # add -DLOB_NATIVE=ON for -march=native
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# or plain make (binaries land in build/bin)
make -j            # make NATIVE=1 / make SANITIZE=1
make test
make bench
```

Tools:

```bash
./build/demo                                                # API walkthrough
./build/replay data/sample_events.txt --max-price 20000     # replay an event file
./build/gen_workload events.txt 5000000 42                  # synthetic 5M-event file
./build/replay events.txt --max-price 200000 --trades trades.txt
./build/throughput_benchmark 50000000 50000000
./build/latency_benchmark 20000000
./build/cancel_benchmark 10000000 1000000
./build/order_index_benchmark 10000000
```

Event file format (one event per line, `#` comments):

```
ADD    <id> <BUY|SELL> <price> <qty>     resting GTC limit
IOC    <id> <BUY|SELL> <price> <qty>     immediate-or-cancel limit
FOK    <id> <BUY|SELL> <price> <qty>     fill-or-kill limit
MARKET <id> <BUY|SELL> <qty>
CANCEL <id>
MODIFY <id> <price> <qty>                cancel/replace
```

## Using the library

```cpp
#include "lob/matching_engine.hpp"

lob::OrderBook      book(/*min_price=*/1, /*max_price=*/20'000);
lob::MatchingEngine engine(book);

engine.submit({1, 10'000, 500, lob::Side::Sell}, [](const lob::Trade&) {});
engine.submit({2,  9'990, 400, lob::Side::Buy},  [](const lob::Trade&) {});

std::vector<lob::Trade> trades;
auto r = engine.submit({3, 10'000, 800, lob::Side::Buy,
                        lob::OrderType::Limit, lob::TimeInForce::IOC}, trades);
// r.status == PartiallyFilled, r.filled == 500, r.resting == 0, trades.size() == 1

engine.modify(2, 9'990, 100, trades);   // reduction keeps queue priority
engine.cancel(2);
```

## Project layout

```
include/lob/
  types.hpp              Order, Trade, enums, SubmitResult
  order_pool.hpp         slab allocator, 32-byte nodes, free list
  order_index.hpp        open-addressing OrderId -> slot table
  price_level.hpp        32-byte level header (intrusive FIFO)
  book_side.hpp          dense per-tick levels + bitmap best-price search
  order_book.hpp         resting-order storage: add / cancel / reduce / fill
  matching_engine.hpp    crossing logic, order types, modify semantics
  market_event.hpp       feed event struct
  event_parser.hpp       text format parser (std::from_chars)
  market_relay.hpp       event dispatch + statistics
  workload.hpp           deterministic synthetic order-flow generator
  latency_histogram.hpp  1 ns histogram with percentiles
src/                     demo.cpp, replay.cpp
tools/                   gen_workload.cpp
tests/                   unit tests, reference model, differential test
benchmarks/              throughput, latency, cancel, index micro-benchmark
docs/DESIGN.md           design rationale, trade-offs, rejected experiments
.github/workflows/       CI: GCC release, GCC ASan+UBSan, Clang release
```

## Testing

* `order_index_test`: probe chains, growth, backward-shift deletion, and
  300k random operations checked against `std::unordered_map`.
* `order_book_test`: level aggregates, best-price tracking across bitmap
  words, FIFO integrity under head/middle/tail cancels, slot reuse.
* `matching_engine_test`: every order type, partial/complete fills,
  multi-level sweeps, price improvement, FIFO priority, all reject paths,
  modify priority rules, modify that crosses the spread.
* `differential_test`: the engine and a naive reference book consume the same
  seeded random stream (with injected edge cases); results, trades and the
  full book state must agree. Run `./build/differential_test <seed> <events>`
  for a custom case.

## Limitations and next steps

* The price range is fixed per book (32 bytes per tick per side). A
  production system would re-centre or use a tree for far tails.
* Single instrument, single thread. Multi-symbol routing, a lock-free SPSC
  ingress queue and core pinning are the natural next layer.
* No self-trade prevention, stop or iceberg orders.
* Text feed only; an ITCH/OUCH binary decoder would replace `EventParser`.
