// Differential (model-based) test: drives the optimised engine and the naive
// ReferenceBook with the same random event stream and checks that every
// result, every trade and the full book state agree.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "lob/market_event.hpp"
#include "lob/matching_engine.hpp"
#include "lob/workload.hpp"
#include "reference_book.hpp"
#include "test_framework.hpp"

using namespace lob;
using lob::test::ReferenceBook;

namespace {

bool same_result(const SubmitResult& a, const SubmitResult& b) {
    return a.status == b.status && a.reason == b.reason && a.filled == b.filled && a.resting == b.resting;
}

bool same_trades(const std::vector<Trade>& a, const std::vector<Trade>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].buy_order_id != b[i].buy_order_id || a[i].sell_order_id != b[i].sell_order_id ||
            a[i].price != b[i].price || a[i].quantity != b[i].quantity || a[i].aggressor != b[i].aggressor)
            return false;
    }
    return true;
}

ReferenceBook::LevelSnapshot snapshot(const OrderBook& book, Side s) {
    ReferenceBook::LevelSnapshot out;
    book.for_each_level(s, [&](const PriceLevel& lvl) {
        std::vector<std::pair<OrderId, Quantity>> orders;
        std::uint64_t total = 0;
        book.for_each_order(lvl, [&](const OrderNode& n) {
            orders.emplace_back(n.id, n.quantity);
            total += n.quantity;
        });
        // Also validate the level's cached aggregates against its contents.
        CHECK_EQ(total, lvl.total_quantity);
        CHECK_EQ(orders.size(), static_cast<std::size_t>(lvl.order_count));
        out.emplace_back(lvl.price, std::move(orders));
        return true;
    });
    return out;
}

std::string describe(const MarketEvent& e) {
    return std::string(to_string(e.type)) + " id=" + std::to_string(e.order_id) + " side=" + to_string(e.side) +
           " px=" + std::to_string(e.price) + " qty=" + std::to_string(e.quantity);
}

Order to_order(const MarketEvent& e) {
    switch (e.type) {
        case EventType::Add:    return Order{e.order_id, e.price, e.quantity, e.side, OrderType::Limit, TimeInForce::GTC};
        case EventType::Ioc:    return Order{e.order_id, e.price, e.quantity, e.side, OrderType::Limit, TimeInForce::IOC};
        case EventType::Fok:    return Order{e.order_id, e.price, e.quantity, e.side, OrderType::Limit, TimeInForce::FOK};
        case EventType::Market: return Order{e.order_id, 0, e.quantity, e.side, OrderType::Market, TimeInForce::IOC};
        default:                return Order{};
    }
}

void run_differential(std::uint64_t seed, std::size_t events, Price min_price, Price max_price) {
    WorkloadConfig cfg;
    cfg.seed = seed;
    cfg.min_price = min_price;
    cfg.max_price = max_price;
    cfg.start_mid = (min_price + max_price) / 2;
    cfg.max_offset = 30;
    cfg.max_quantity = 50;
    cfg.add_pct = 40; cfg.aggressive_pct = 20; cfg.ioc_pct = 5; cfg.market_pct = 3;
    cfg.cancel_pct = 22; cfg.modify_pct = 10;
    cfg.drift_prob = 0.01;
    WorkloadGenerator gen(cfg);

    OrderBook book(min_price, max_price, 64);
    MatchingEngine engine(book);
    ReferenceBook ref(min_price, max_price);

    std::vector<Trade> got, want;
    std::mt19937_64 rng(seed ^ 0xABCDEF);

    for (std::size_t i = 0; i < events; ++i) {
        MarketEvent e = gen.next();

        // Occasionally inject edge cases the generator never produces.
        const int edge = static_cast<int>(rng() % 200);
        if (edge == 0) e.quantity = 0;
        else if (edge == 1 && e.type != EventType::Cancel && e.type != EventType::Market) e.price = max_price + 1;
        else if (edge == 2 && e.type == EventType::Add) e.order_id = 1;  // likely duplicate
        else if (edge == 3 && e.type == EventType::Add) e.type = EventType::Fok;
        else if (edge == 4 && e.type == EventType::Modify) e.quantity = 1;  // reduction path

        bool ok = true;
        switch (e.type) {
            case EventType::Cancel: {
                const bool a = engine.cancel(e.order_id);
                const bool b = ref.cancel(e.order_id);
                ok = (a == b);
                break;
            }
            case EventType::Modify: {
                const SubmitResult a = engine.modify(e.order_id, e.price, e.quantity, got);
                const SubmitResult b = ref.modify(e.order_id, e.price, e.quantity, want);
                ok = same_result(a, b) && same_trades(got, want);
                break;
            }
            default: {
                const Order o = to_order(e);
                const SubmitResult a = engine.submit(o, got);
                const SubmitResult b = ref.submit(o, want);
                ok = same_result(a, b) && same_trades(got, want);
                break;
            }
        }

        if (!ok) {
            std::fprintf(stderr, "    divergence at event %zu (seed %llu): %s\n", i,
                         static_cast<unsigned long long>(seed), describe(e).c_str());
            REQUIRE(ok);
        }

        // Full-state comparison periodically and at the end (it is O(book)).
        if (i % 997 == 0 || i + 1 == events) {
            CHECK_EQ(book.order_count(), ref.order_count());
            CHECK(snapshot(book, Side::Buy) == ref.snapshot(Side::Buy));
            CHECK(snapshot(book, Side::Sell) == ref.snapshot(Side::Sell));
        } else {
            // Cheap top-of-book comparison every step.
            const auto rb = ref.snapshot(Side::Buy);
            const auto ra = ref.snapshot(Side::Sell);
            CHECK_EQ(book.has_bids(), !rb.empty());
            CHECK_EQ(book.has_asks(), !ra.empty());
            if (book.has_bids() && !rb.empty()) CHECK_EQ(book.best_bid_price(), rb.front().first);
            if (book.has_asks() && !ra.empty()) CHECK_EQ(book.best_ask_price(), ra.front().first);
        }
        if (lob::test::failures() > 0) {
            std::fprintf(stderr, "    state mismatch at event %zu (seed %llu)\n", i,
                         static_cast<unsigned long long>(seed));
            throw lob::test::RequireFailed{};
        }
    }
}

}  // namespace

TEST(differential_narrow_book_heavy_crossing) {
    // Tiny price range: levels are constantly created and emptied.
    run_differential(1, 60'000, 100, 180);
}

TEST(differential_wide_book) {
    run_differential(2, 60'000, 1, 20'000);
}

TEST(differential_many_seeds_short_runs) {
    for (std::uint64_t seed = 10; seed < 40; ++seed) run_differential(seed, 5'000, 50, 250);
}

int main(int argc, char** argv) {
    // Optional: ./differential_test <seed> <events> runs one long custom case.
    if (argc == 3) {
        run_differential(std::strtoull(argv[1], nullptr, 10), std::strtoull(argv[2], nullptr, 10), 1, 20'000);
        return lob::test::failures() == 0 ? 0 : 1;
    }
    return lob::test::run_all();
}
