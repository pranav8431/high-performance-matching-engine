// Throughput of the matching engine on two workloads:
//
//  1. "uniform crossing": N limit orders, alternating sides, price uniform in
//     [95, 105], quantity uniform in [1, 100]. This is the workload used by the
//     original std::map based implementation, kept for apples-to-apples
//     comparison. Almost every order crosses, so it stresses matching.
//
//  2. "realistic flow": the WorkloadGenerator mix of passive adds, cancels,
//     modifies and aggressive orders around a drifting mid.
//
// Workloads are generated before the timer starts; only engine time is measured.

#include <cstdio>
#include <random>
#include <vector>

#include "bench_common.hpp"
#include "lob/market_relay.hpp"
#include "lob/matching_engine.hpp"
#include "lob/workload.hpp"

using namespace lob;
using namespace lob::bench;

static void uniform_crossing(std::size_t n) {
    std::vector<Order> orders;
    orders.reserve(n);
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<Price> price(95, 105);
    std::uniform_int_distribution<Quantity> qty(1, 100);
    for (OrderId i = 1; i <= n; ++i) {
        orders.push_back(Order{i, price(rng), qty(rng), (i % 2 == 0) ? Side::Buy : Side::Sell,
                               OrderType::Limit, TimeInForce::GTC});
    }

    OrderBook book(1, 1000, 1 << 20);
    MatchingEngine engine(book);
    std::uint64_t trades = 0;

    const auto start = Clock::now();
    for (const Order& o : orders) {
        engine.submit(o, [&trades](const Trade&) { ++trades; });
    }
    const double secs = seconds_since(start);

    print_rate("uniform crossing (limit orders)", n, secs);
    std::printf("  %-34s %14s\n", "  trades generated", with_commas(trades).c_str());
    std::printf("  %-34s %14s\n", "  orders resting at end", with_commas(book.order_count()).c_str());
}

static void realistic_flow(std::size_t n) {
    WorkloadConfig cfg;
    WorkloadGenerator gen(cfg);
    std::vector<MarketEvent> events = gen.generate(n);

    OrderBook book(cfg.min_price, cfg.max_price, 1 << 20);
    MatchingEngine engine(book);
    MarketRelay<> relay(engine);

    const auto start = Clock::now();
    for (const MarketEvent& e : events) relay.process(e);
    const double secs = seconds_since(start);

    const RelayStats& s = relay.stats();
    print_rate("realistic flow (mixed events)", n, secs);
    std::printf("  %-34s %14s\n", "  adds (passive + aggressive)", with_commas(s.adds).c_str());
    std::printf("  %-34s %14s\n", "  cancels (hits / misses)",
                (with_commas(s.cancels - s.cancel_misses) + " / " + with_commas(s.cancel_misses)).c_str());
    std::printf("  %-34s %14s\n", "  modifies (hits / misses)",
                (with_commas(s.modifies - s.modify_misses) + " / " + with_commas(s.modify_misses)).c_str());
    std::printf("  %-34s %14s\n", "  ioc + market", with_commas(s.iocs + s.markets).c_str());
    std::printf("  %-34s %14s\n", "  trades generated", with_commas(s.trades).c_str());
    std::printf("  %-34s %14s\n", "  orders resting at end", with_commas(book.order_count()).c_str());
    std::printf("  %-34s %14s\n", "  non-empty levels (bid / ask)",
                (with_commas(book.level_count(Side::Buy)) + " / " + with_commas(book.level_count(Side::Sell))).c_str());
}

int main(int argc, char** argv) {
    const std::size_t uniform_n = arg_size(argc, argv, 1, 50'000'000);
    const std::size_t flow_n    = arg_size(argc, argv, 2, 50'000'000);
    print_build_info();
    std::printf("\nThroughput (workload pre-generated, engine time only)\n\n");
    uniform_crossing(uniform_n);
    std::printf("\n");
    realistic_flow(flow_n);
    return 0;
}
