# high-performance-matching-engine
### Initital benchmark
    Baseline Benchmark
    -------------------
    Compiler: g++ -O3
    Language: C++17
    Orders: 1,000,000

    Average throughput: ~14.4 million orders/sec
    -----------------------------------------------------------------------------

    Orders processed : 5000000
    Time taken       : 0.333842 seconds
    Throughput       : 14.9772 million orders/sec


    -----------------------------------------------------------------------------

    Orders processed : 10000000
    Time taken       : 0.68993 seconds
    Throughput       : 14.4942 million orders/sec


    -----------------------------------------------------------------------------

    Orders processed : 50000000
    Time taken       : 3.34039 seconds
    Throughput       : 14.9683 million orders/sec


    -----------------------------------------------------------------------------
## Baseline Benchmark

Compiler: g++ -O3
Standard: C++17

| Orders | Time (s) | Throughput |
|-------:|---------:|-----------:|
| 1M     | ~0.07    | ~14.4 M/s  |
| 5M     | 0.333842 | 14.98 M/s  |
| 10M    | 0.689930 | 14.49 M/s  |
| 50M    | 3.340390 | 14.97 M/s  |


## Performance Benchmarks

Benchmarks were performed locally using:

- **Compiler:** g++ with `-O3`
- **Language:** C++17
- **Workload:** Synthetic limit orders with deterministic random generation
- **Hardware:** Local development machine
- **Benchmark methodology:** Input orders and cancellation IDs are generated before timing; only the target operation is included in the measured interval.

### Matching Engine

| Orders | Time | Throughput |
|-------:|------:|-----------:|
| 1,000,000 | ~0.07 s | ~14.4 M orders/s |
| 5,000,000 | 0.333842 s | 14.98 M orders/s |
| 10,000,000 | 0.689930 s | 14.49 M orders/s |
| 50,000,000 | 3.340390 s | 14.97 M orders/s |

The matching workload maintains approximately **14.4–15.0 million orders/sec** across larger workloads, indicating near-linear scaling for the tested workload.

### Cancellation

The order book uses an `unordered_map<OrderId, OrderLocation>` combined with a `std::list<Order>` iterator to locate orders directly for cancellation.

| Orders | Cancellation Requests | Successful Cancels | Time | Cancellation Rate |
|-------:|----------------------:|-------------------:|------:|-------------------:|
| 1,000,000 | 500,000 | 393,449 | 0.151932 s | 3.29 M/s |
| 10,000,000 | 1,000,000 | 951,890 | 0.422629 s | 2.37 M/s |

The cancellation benchmark demonstrates direct order lookup through the order-ID index rather than scanning the entire order book.

> **Note:** These are local benchmark results from synthetic workloads and should not be interpreted as exchange-grade performance figures. Results depend on hardware, compiler, workload distribution, and system state.