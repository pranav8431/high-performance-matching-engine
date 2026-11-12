// Per-operation latency distribution on the realistic workload.
//
// Each event is timed individually with rdtsc (see TscTimer) and the sample
// goes into a 1 ns resolution histogram keyed by event type. The timer's own
// overhead (printed) is included in every sample, so numbers are slightly
// pessimistic. The order index and pool are pre-sized so the run contains no
// rehash or reallocation; see the README for what happens when they are not.

#include <cstdio>
#include <vector>

#include "bench_common.hpp"
#include "lob/latency_histogram.hpp"
#include "lob/market_relay.hpp"
#include "lob/matching_engine.hpp"
#include "lob/workload.hpp"

using namespace lob;
using namespace lob::bench;

int main(int argc, char** argv) {
    const std::size_t n = arg_size(argc, argv, 1, 20'000'000);
    const std::size_t expected_orders = arg_size(argc, argv, 2, 1 << 21);  // peak resting is ~1.3M for 20M events
    print_build_info();
    TscTimer timer;
    std::printf("timer: %s, %.3f GHz, start/stop overhead %llu ns\n", timer.source(), timer.ghz(),
                static_cast<unsigned long long>(timer.overhead_ns()));

    WorkloadConfig cfg;
    WorkloadGenerator gen(cfg);
    std::vector<MarketEvent> events = gen.generate(n);

    OrderBook book(cfg.min_price, cfg.max_price, expected_orders);
    MatchingEngine engine(book);
    MarketRelay<> relay(engine);

    LatencyHistogram all, add, aggressive, cancel, modify;

    // Warm-up: fault in the level arrays and let the hash table reach its
    // steady-state size so the measured region excludes one-off costs.
    const std::size_t warm = n / 20;
    for (std::size_t i = 0; i < warm; ++i) relay.process(events[i]);

    // Classify adds as passive/aggressive by whether they trade.
    std::uint64_t trades_before = relay.stats().trades;
    for (std::size_t i = warm; i < n; ++i) {
        const MarketEvent& e = events[i];
        const std::uint64_t t0 = TscTimer::start();
        relay.process(e);
        const std::uint64_t t1 = TscTimer::stop();
        const std::uint64_t ns = timer.to_ns(t1 - t0);
        all.record(ns);
        switch (e.type) {
            case EventType::Cancel: cancel.record(ns); break;
            case EventType::Modify: modify.record(ns); break;
            default: {
                const std::uint64_t trades_after = relay.stats().trades;
                if (trades_after != trades_before) aggressive.record(ns); else add.record(ns);
                trades_before = trades_after;
                break;
            }
        }
    }

    std::printf("\nPer-event latency, realistic flow, %s events measured (after %s warm-up)\n\n",
                with_commas(n - warm).c_str(), with_commas(warm).c_str());
    LatencyHistogram::print_header();
    add.print("add (passive)");
    aggressive.print("add (trading)");
    cancel.print("cancel");
    modify.print("modify");
    all.print("all events");
    std::printf("\n  trades: %s   resting at end: %s\n", with_commas(relay.stats().trades).c_str(),
                with_commas(book.order_count()).c_str());
    return 0;
}
