// Cancellation throughput: rest N orders at one price, then time M random
// cancels (some of which target already-cancelled ids and miss). Mirrors the
// original project's cancellation benchmark for before/after comparison.

#include <cstdio>
#include <random>
#include <vector>

#include "bench_common.hpp"
#include "lob/order_book.hpp"

using namespace lob;
using namespace lob::bench;

int main(int argc, char** argv) {
    const std::size_t num_orders  = arg_size(argc, argv, 1, 10'000'000);
    const std::size_t num_cancels = arg_size(argc, argv, 2, 1'000'000);
    print_build_info();

    OrderBook book(1, 1000, num_orders);
    for (OrderId i = 1; i <= num_orders; ++i) {
        book.add(Order{i, 100, 100, Side::Buy, OrderType::Limit, TimeInForce::GTC});
    }

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<OrderId> pick(1, num_orders);
    std::vector<OrderId> ids(num_cancels);
    for (OrderId& id : ids) id = pick(rng);

    std::size_t hits = 0;
    const auto start = Clock::now();
    for (OrderId id : ids) hits += book.cancel(id) ? 1 : 0;
    const double secs = seconds_since(start);

    std::printf("\nCancellation (%s resting orders, %s cancel requests)\n\n",
                with_commas(num_orders).c_str(), with_commas(num_cancels).c_str());
    print_rate("cancel (random ids)", num_cancels, secs);
    std::printf("  %-34s %14s\n", "  successful cancels", with_commas(hits).c_str());
    std::printf("  %-34s %14s\n", "  orders remaining", with_commas(book.order_count()).c_str());
    return 0;
}
