# Design notes

This document explains *why* the engine is built the way it is. The README
covers what it does and how fast it runs.

## 1. Goals

1. **Deterministic, allocation-free hot path.** After warm-up, submitting,
   cancelling or modifying an order performs no heap allocation. Everything the
   engine touches lives in three contiguous arrays.
2. **O(1) for the common operations.** Add, cancel, reduce and fill-at-best are
   constant time. Finding the next best level after one is emptied is a bitmap
   scan, effectively constant for a realistic book.
3. **Correctness that can be demonstrated**, not just asserted: a naive
   reference model and a differential fuzz test, plus sanitizer builds in CI.
4. **Small enough to read in one sitting.** The core is ~900 lines of headers.

## 2. Data layout

```
OrderBook
├── OrderPool      std::vector<OrderNode>        one 32-byte node per resting order
├── OrderIndex     open-addressing hash table    OrderId -> SlotIndex
├── BookSide bids  std::vector<PriceLevel>       one 32-byte header per price tick
│                  std::vector<uint64_t>         bitmap: bit i set <=> level i non-empty
└── BookSide asks  (same)
```

### OrderNode (32 bytes, two per cache line)

| field    | type      | bytes | purpose                                   |
|----------|-----------|------:|-------------------------------------------|
| id       | uint64_t  | 8     | exchange order id                         |
| price    | int64_t   | 8     | needed to find the level on cancel        |
| quantity | uint32_t  | 4     | remaining quantity                        |
| side     | uint8_t   | 1 (+3)| which BookSide owns the node              |
| prev     | uint32_t  | 4     | FIFO link within the price level          |
| next     | uint32_t  | 4     | FIFO link, or free-list link when released|

Links are 32-bit **indices**, not pointers. This halves link size and, more
importantly, keeps every handle valid when the pool vector reallocates. An
iterator- or pointer-based design (the previous version used
`std::list::iterator`) forbids contiguous storage.

### PriceLevel (32 bytes)

`price`, `total_quantity`, `order_count`, `head`, `tail`. The queue of orders
at a price is an intrusive doubly linked list through the pool, so enqueue,
dequeue and unlink-from-the-middle are all O(1) with no allocation.
`total_quantity` is maintained incrementally so depth snapshots and FOK
liquidity checks never walk the queue.

### BookSide: dense levels plus a bitmap

Price levels are a flat array indexed by tick. To avoid a `Side` branch in
every best-price lookup, prices are mapped so that the most aggressive price
is always index 0:

```
bids: index = max_price - price      // highest bid  -> index 0
asks: index = price - min_price      // lowest  ask  -> index 0
```

With that mapping "find the best level" is "find the lowest set bit" on both
sides. When the best level empties, the next best is located by scanning the
bitmap one 64-bit word at a time with `std::countr_zero`. A book that spans
10,000 ticks is a 157-word bitmap; in practice the next non-empty level is
within a few ticks so the scan touches one word.

Memory cost is 32 bytes per tick per side. A $0.01 tick instrument quoted in
[$0.01, $2,000.00] is 200,000 ticks, 6.4 MB per side, mostly untouched pages.

**Trade-off:** the price range is fixed at construction. Orders outside it are
rejected with `PriceOutOfRange`. Exchanges impose price bands anyway; a
production system would re-centre the array on a session boundary or fall back
to a tree for the far tails.

### OrderIndex: open addressing with backward-shift deletion

`std::unordered_map` allocates a node per entry and chases a pointer per
lookup. `OrderIndex` is a single array of `{key, value}` pairs:

* **Fibonacci hashing**: `(id * 2^64/phi) >> (64 - log2(capacity))`. Exchange
  ids are often sequential; the multiply spreads them across the table while
  keeping nearby ids in nearby buckets, which is cache friendly for linear
  probing.
* **Load factor <= 0.5**, doubling on growth. Probe sequences stay short.
* **Backward-shift deletion** instead of tombstones. After a delete, later
  entries of the same probe chain are pulled back so every lookup still stops at
  the first empty bucket. Tables that use tombstones get slower as cancels
  accumulate, which is exactly the workload of an order book.

`benchmarks/order_index_benchmark.cpp` compares it to `std::unordered_map`.

#### Experiment: exploiting monotonic order ids (rejected)

Exchange order ids are nearly sequential, so the Fibonacci scatter turns a
sequential insert / FIFO-fill stream into one cache miss per order. Two
alternatives were measured (20M events, same machine as the README):

| hash                                   | uniform crossing | realistic flow | random cancels (10M book) |
|----------------------------------------|-----------------:|---------------:|--------------------------:|
| Fibonacci (shipped)                    | 9.1 M/s          | 10.2 M/s       | 7.1 M/s                   |
| identity (`id & mask`)                 | 20.4 M/s         | **0.28 M/s**   | **1.7 M/s**               |
| Fibonacci per 64-byte line, 4 ids/line | 11.8 M/s         | 10.5 M/s       | 4.3 M/s                   |

* **Identity** is catastrophic under linear probing: sequential ids form one
  contiguous run, so every miss (a cancel for an id that has already traded)
  and every backward-shift delete scans to the end of the run. The realistic
  workload, where ~40% of cancels miss, slowed down 35x.
* **Line grouping** (hash `id / 4` to pick a cache line, `id % 4` for the slot
  inside it) keeps the scatter but makes FIFO fills touch one line per four
  orders. It gains 30% on the crossing-heavy workload and nothing on the
  realistic one, while random cancels lose 40% because fully packed lines
  lengthen the occupied runs that backward-shift deletion must walk.

The plain Fibonacci table was kept: it has no bad case, and the realistic
workload is bound by the matching loop rather than by the index.

## 3. Matching semantics

Price-time priority. An incoming order walks the opposite side from the best
level, oldest order first, and executes at the **resting** order's price.

| type / tif        | behaviour of unfilled remainder                      |
|-------------------|------------------------------------------------------|
| Limit GTC         | rests at the back of its price level                 |
| Limit IOC         | discarded                                            |
| Limit FOK         | liquidity is checked first; either fills completely or is rejected without touching the book |
| Market            | sweeps any price; remainder discarded                |

Validation happens **before** any fill: duplicate id, zero quantity and
out-of-range price are rejected up front so a rejected order can never leave a
partial execution behind.

### Cancel / replace

`modify(id, new_price, new_qty)`:

* same price and **smaller** quantity -> reduced in place, queue position kept
  (this matches NASDAQ/CME treatment of size reductions);
* anything else -> cancelled and resubmitted as a new GTC limit with the same
  id. It goes to the back of the queue and, if it now crosses, it trades.

## 4. Complexity

| operation                          | cost                                   |
|------------------------------------|----------------------------------------|
| add (rest)                         | O(1): hash insert + pool alloc + link  |
| cancel                             | O(1): hash find + unlink + hash erase  |
| reduce quantity                    | O(1)                                   |
| fill front of best level           | O(1)                                   |
| next best level after emptying     | O(levels / 64) bitmap scan, ~O(1)      |
| submit crossing k resting orders   | O(k)                                   |
| FOK liquidity check over L levels  | O(L)                                   |

Hash-table growth and pool-vector growth are O(n) events; both are avoidable
by sizing `expected_orders` at construction.

## 5. Event delivery

`MatchingEngine::submit` takes a callable and invokes it once per execution.
The engine never allocates a trade list; callers that want a vector use the
convenience overload. `MarketRelay` layers event-type dispatch and statistics
on top and is what the replay tool and benchmarks drive.

## 6. Testing strategy

1. **Unit tests** per component (`tests/*_test.cpp`): index semantics
   including backward-shift chains, book invariants, every order type and
   every reject path.
2. **Differential fuzzing** (`tests/differential_test.cpp`): a naive
   `ReferenceBook` built from `std::map` + `std::deque` + `std::unordered_map`
   implements the same semantics in the most obvious way possible. Both engines
   consume the same random stream (with injected edge cases: zero quantity,
   out-of-range prices, duplicate ids, FOK, reductions). After every event the
   results and trades must match exactly; periodically the entire book,
   including cached level aggregates, is compared.
3. **Sanitizers**: CI builds and runs the suite with ASan + UBSan.
4. **Determinism**: all workloads are seeded, so a failing seed reproduces.

## 7. Measuring latency

`benchmarks/latency_benchmark.cpp` times every event individually. The first
version used `std::chrono::steady_clock` and reported a suspiciously flat
1.4 us median for every operation: the development machine's clocksource is
`hpet`, so each `clock_gettime` call is a ~1.2 us system call. The benchmark
now uses `rdtsc`/`rdtscp` (fenced, calibrated against `steady_clock`, ~10 ns
per start/stop pair, printed at start-up) and the histogram is 1 ns resolution.

The index and pool are pre-sized for the run. When they are not, the trace
shows single events of tens of milliseconds: a hash-table rehash copying
millions of entries and a `std::vector` reallocation of the pool. The
throughput number barely moves, the p99.99 does not move, only the max does,
which is exactly why tail latency has to be measured and not inferred.

## 8. What a production version would add

* Multiple instruments (one `OrderBook` per symbol behind a symbol table).
* A lock-free SPSC queue between the feed-handler thread and the engine
  thread, with the engine pinned to an isolated core.
* A binary feed (ITCH / OUCH) decoder in place of the text parser.
* Self-trade prevention, stop orders, post-only, iceberg orders.
* Journaling of inbound events for crash recovery and deterministic replay.
* Pin the engine thread and use an isolated core; the benchmark numbers in
  the README are from an ordinary desktop session.
