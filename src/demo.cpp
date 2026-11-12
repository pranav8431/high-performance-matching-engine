// Minimal walkthrough of the public API: rest some orders, submit an
// aggressive order, print the trades and the resulting book.

#include <algorithm>
#include <cstdio>
#include <vector>

#include "lob/matching_engine.hpp"

using namespace lob;

static void print_book(const OrderBook& book) {
    std::printf("\n  %-10s %10s %8s | %-8s %10s %10s\n", "bid qty", "bid", "orders", "orders", "ask", "ask qty");
    std::printf("  ----------------------------------------------------------------\n");
    std::vector<const PriceLevel*> bids, asks;
    book.for_each_level(Side::Buy,  [&](const PriceLevel& l) { bids.push_back(&l); return true; });
    book.for_each_level(Side::Sell, [&](const PriceLevel& l) { asks.push_back(&l); return true; });
    for (std::size_t i = 0; i < std::max(bids.size(), asks.size()); ++i) {
        if (i < bids.size()) std::printf("  %-10llu %10lld %8u | ", (unsigned long long)bids[i]->total_quantity, (long long)bids[i]->price, bids[i]->order_count);
        else                 std::printf("  %-10s %10s %8s | ", "", "", "");
        if (i < asks.size()) std::printf("%-8u %10lld %10llu\n", asks[i]->order_count, (long long)asks[i]->price, (unsigned long long)asks[i]->total_quantity);
        else                 std::printf("\n");
    }
    std::printf("\n");
}

int main() {
    OrderBook book(/*min_price=*/1, /*max_price=*/20'000);
    MatchingEngine engine(book);
    std::vector<Trade> trades;

    // Build a small book. Prices are integer ticks (e.g. cents).
    engine.submit(Order{1, 10'000, 500, Side::Sell}, trades);
    engine.submit(Order{2, 10'010, 300, Side::Sell}, trades);
    engine.submit(Order{3, 10'020, 800, Side::Sell}, trades);
    engine.submit(Order{4,  9'990, 400, Side::Buy},  trades);
    engine.submit(Order{5,  9'980, 600, Side::Buy},  trades);

    std::printf("Initial book:");
    print_book(book);

    // Aggressive buy that sweeps two levels and rests the remainder.
    const Order buy{6, 10'010, 1'000, Side::Buy, OrderType::Limit, TimeInForce::GTC};
    const SubmitResult r = engine.submit(buy, trades);
    std::printf("Submitted BUY 1000 @ 10010 -> %s (filled %u, resting %u)\n",
                to_string(r.status), r.filled, r.resting);
    for (const Trade& t : trades) {
        std::printf("  trade: buy #%llu x sell #%llu  %u @ %lld  (aggressor %s)\n",
                    (unsigned long long)t.buy_order_id, (unsigned long long)t.sell_order_id,
                    t.quantity, (long long)t.price, to_string(t.aggressor));
    }
    print_book(book);

    // Reduce quantity in place (keeps queue position), then cancel.
    engine.modify(6, 10'010, 100, trades);
    std::printf("Reduced order 6 to 100 (priority kept).\n");
    engine.cancel(5);
    std::printf("Cancelled order 5.\n");
    print_book(book);

    // IOC that only partially fills and discards the rest.
    const SubmitResult ioc = engine.submit(Order{7, 10'015, 5'000, Side::Buy, OrderType::Limit, TimeInForce::IOC}, trades);
    std::printf("IOC BUY 5000 @ 10015 -> %s (filled %u, resting %u)\n", to_string(ioc.status), ioc.filled, ioc.resting);
    print_book(book);
    return 0;
}
