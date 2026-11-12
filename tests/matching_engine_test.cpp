#include <vector>

#include "lob/matching_engine.hpp"
#include "test_framework.hpp"

using namespace lob;

namespace {
Order limit(OrderId id, Side side, Price price, Quantity qty, TimeInForce tif = TimeInForce::GTC) {
    return Order{id, price, qty, side, OrderType::Limit, tif};
}
Order market(OrderId id, Side side, Quantity qty) {
    return Order{id, 0, qty, side, OrderType::Market, TimeInForce::IOC};
}

struct Fixture {
    OrderBook book{1, 1000};
    MatchingEngine engine{book};
    std::vector<Trade> trades;
    SubmitResult submit(const Order& o) { return engine.submit(o, trades); }
    void rest(const Order& o) { REQUIRE(book.add(o) == RejectReason::None); }
};

void check_trade(const Trade& t, OrderId buy, OrderId sell, Price price, Quantity qty) {
    CHECK_EQ(t.buy_order_id, buy);
    CHECK_EQ(t.sell_order_id, sell);
    CHECK_EQ(t.price, price);
    CHECK_EQ(t.quantity, qty);
}
}  // namespace

TEST(partial_fill_leaves_remainder_on_book) {
    Fixture f;
    f.rest(limit(1, Side::Sell, 100, 500));
    const SubmitResult r = f.submit(limit(2, Side::Buy, 100, 300));
    CHECK(r.status == SubmitStatus::Filled);
    CHECK_EQ(r.filled, Quantity{300});
    REQUIRE(f.trades.size() == 1);
    check_trade(f.trades[0], 2, 1, 100, 300);
    CHECK(f.trades[0].aggressor == Side::Buy);
    REQUIRE(f.book.has_asks());
    CHECK_EQ(f.book.front(*f.book.best_ask()).quantity, Quantity{200});
}

TEST(complete_fill_removes_resting_order) {
    Fixture f;
    f.rest(limit(1, Side::Sell, 100, 500));
    const SubmitResult r = f.submit(limit(2, Side::Buy, 100, 500));
    CHECK(r.status == SubmitStatus::Filled);
    REQUIRE(f.trades.size() == 1);
    check_trade(f.trades[0], 2, 1, 100, 500);
    CHECK(!f.book.has_asks());
    CHECK(!f.book.contains(1));
    CHECK(f.book.empty());
}

TEST(sweeps_multiple_price_levels_best_first) {
    Fixture f;
    f.rest(limit(1, Side::Sell, 100, 500));
    f.rest(limit(2, Side::Sell, 101, 300));
    const SubmitResult r = f.submit(limit(3, Side::Buy, 101, 700));
    CHECK(r.status == SubmitStatus::Filled);
    REQUIRE(f.trades.size() == 2);
    check_trade(f.trades[0], 3, 1, 100, 500);
    check_trade(f.trades[1], 3, 2, 101, 200);
    CHECK_EQ(f.book.best_ask_price(), Price{101});
    CHECK_EQ(f.book.front(*f.book.best_ask()).quantity, Quantity{100});
}

TEST(non_crossing_limit_rests) {
    Fixture f;
    f.rest(limit(1, Side::Sell, 100, 500));
    const SubmitResult r = f.submit(limit(2, Side::Buy, 99, 300));
    CHECK(r.status == SubmitStatus::Rested);
    CHECK_EQ(r.filled, Quantity{0});
    CHECK_EQ(r.resting, Quantity{300});
    CHECK(f.trades.empty());
    CHECK_EQ(f.book.best_bid_price(), Price{99});
    CHECK_EQ(f.book.best_ask_price(), Price{100});
}

TEST(partial_fill_then_rest_remainder) {
    Fixture f;
    f.rest(limit(1, Side::Sell, 100, 200));
    const SubmitResult r = f.submit(limit(2, Side::Buy, 100, 500));
    CHECK(r.status == SubmitStatus::PartiallyFilled);
    CHECK_EQ(r.filled, Quantity{200});
    CHECK_EQ(r.resting, Quantity{300});
    CHECK(!f.book.has_asks());
    REQUIRE(f.book.has_bids());
    CHECK_EQ(f.book.best_bid_price(), Price{100});
    CHECK_EQ(f.book.front(*f.book.best_bid()).quantity, Quantity{300});
}

TEST(price_time_priority_fifo_within_level) {
    Fixture f;
    f.rest(limit(1, Side::Sell, 100, 300));
    f.rest(limit(2, Side::Sell, 100, 500));
    f.submit(limit(3, Side::Buy, 100, 400));
    REQUIRE(f.trades.size() == 2);
    check_trade(f.trades[0], 3, 1, 100, 300);
    check_trade(f.trades[1], 3, 2, 100, 100);
    CHECK_EQ(f.book.front(*f.book.best_ask()).id, OrderId{2});
    CHECK_EQ(f.book.front(*f.book.best_ask()).quantity, Quantity{400});
}

TEST(trade_price_is_resting_price_price_improvement) {
    Fixture f;
    f.rest(limit(1, Side::Buy, 105, 100));
    f.submit(limit(2, Side::Sell, 100, 100));  // willing to sell at 100, bid is 105
    REQUIRE(f.trades.size() == 1);
    check_trade(f.trades[0], 1, 2, 105, 100);
    CHECK(f.trades[0].aggressor == Side::Sell);
}

TEST(market_buy_sweeps_and_discards_remainder) {
    Fixture f;
    f.rest(limit(1, Side::Sell, 100, 300));
    f.rest(limit(2, Side::Sell, 101, 300));
    const SubmitResult r = f.submit(market(3, Side::Buy, 500));
    CHECK(r.status == SubmitStatus::Filled);
    REQUIRE(f.trades.size() == 2);
    check_trade(f.trades[0], 3, 1, 100, 300);
    check_trade(f.trades[1], 3, 2, 101, 200);
    CHECK_EQ(f.book.front(*f.book.best_ask()).quantity, Quantity{100});

    const SubmitResult r2 = f.submit(market(4, Side::Buy, 500));
    CHECK(r2.status == SubmitStatus::PartiallyFilled);
    CHECK_EQ(r2.filled, Quantity{100});
    CHECK_EQ(r2.resting, Quantity{0});
    CHECK(f.book.empty());  // nothing rests after a market order
}

TEST(market_sell_sweeps_bids) {
    Fixture f;
    f.rest(limit(1, Side::Buy, 100, 300));
    f.rest(limit(2, Side::Buy, 99, 300));
    f.submit(market(3, Side::Sell, 500));
    REQUIRE(f.trades.size() == 2);
    check_trade(f.trades[0], 1, 3, 100, 300);
    check_trade(f.trades[1], 2, 3, 99, 200);
    CHECK_EQ(f.book.best_bid_price(), Price{99});
    CHECK_EQ(f.book.front(*f.book.best_bid()).quantity, Quantity{100});
}

TEST(market_order_on_empty_book_is_cancelled) {
    Fixture f;
    const SubmitResult r = f.submit(market(1, Side::Buy, 500));
    CHECK(r.status == SubmitStatus::Cancelled);
    CHECK(f.trades.empty());
    CHECK(f.book.empty());
}

TEST(ioc_fills_what_it_can_and_discards_the_rest) {
    Fixture f;
    f.rest(limit(1, Side::Sell, 100, 200));
    f.rest(limit(2, Side::Sell, 102, 200));
    const SubmitResult r = f.submit(limit(3, Side::Buy, 101, 500, TimeInForce::IOC));
    CHECK(r.status == SubmitStatus::PartiallyFilled);
    CHECK_EQ(r.filled, Quantity{200});
    CHECK_EQ(r.resting, Quantity{0});
    CHECK(!f.book.has_bids());
    CHECK_EQ(f.book.best_ask_price(), Price{102});

    const SubmitResult r2 = f.submit(limit(4, Side::Buy, 101, 500, TimeInForce::IOC));
    CHECK(r2.status == SubmitStatus::Cancelled);
    CHECK(!f.book.has_bids());
}

TEST(fok_executes_fully_or_not_at_all) {
    Fixture f;
    f.rest(limit(1, Side::Sell, 100, 200));
    f.rest(limit(2, Side::Sell, 101, 200));
    f.rest(limit(3, Side::Sell, 150, 1000));  // too expensive for a 101 limit

    const SubmitResult killed = f.submit(limit(4, Side::Buy, 101, 500, TimeInForce::FOK));
    CHECK(killed.status == SubmitStatus::Rejected);
    CHECK(killed.reason == RejectReason::InsufficientLiquidity);
    CHECK(f.trades.empty());
    CHECK_EQ(f.book.order_count(), std::size_t{3});  // untouched

    const SubmitResult filled = f.submit(limit(5, Side::Buy, 101, 400, TimeInForce::FOK));
    CHECK(filled.status == SubmitStatus::Filled);
    REQUIRE(f.trades.size() == 2);
    check_trade(f.trades[0], 5, 1, 100, 200);
    check_trade(f.trades[1], 5, 2, 101, 200);
    CHECK_EQ(f.book.best_ask_price(), Price{150});
}

TEST(rejects_duplicate_zero_and_out_of_range) {
    Fixture f;
    CHECK(f.submit(limit(1, Side::Buy, 100, 0)).reason == RejectReason::ZeroQuantity);
    CHECK(f.submit(limit(1, Side::Buy, 0, 10)).reason == RejectReason::PriceOutOfRange);
    CHECK(f.submit(limit(1, Side::Buy, 1001, 10)).reason == RejectReason::PriceOutOfRange);
    CHECK(f.submit(limit(1, Side::Buy, 100, 10)).status == SubmitStatus::Rested);
    const SubmitResult dup = f.submit(limit(1, Side::Sell, 100, 10));
    CHECK(dup.status == SubmitStatus::Rejected);
    CHECK(dup.reason == RejectReason::DuplicateId);
    CHECK(f.trades.empty());  // the duplicate must not have traded against order 1
    CHECK_EQ(f.book.order_count(), std::size_t{1});
    CHECK_EQ(f.book.front(*f.book.best_bid()).quantity, Quantity{10});
}

TEST(cancel_through_engine) {
    Fixture f;
    f.submit(limit(1, Side::Sell, 100, 500));
    CHECK(f.engine.cancel(1));
    CHECK(!f.engine.cancel(1));
    CHECK(!f.engine.cancel(999));
    CHECK(f.book.empty());
}

TEST(cancel_middle_of_queue) {
    Fixture f;
    f.rest(limit(1, Side::Sell, 100, 100));
    f.rest(limit(2, Side::Sell, 100, 200));
    f.rest(limit(3, Side::Sell, 100, 300));
    CHECK(f.engine.cancel(2));
    f.submit(limit(4, Side::Buy, 100, 350));
    REQUIRE(f.trades.size() == 2);
    check_trade(f.trades[0], 4, 1, 100, 100);
    check_trade(f.trades[1], 4, 3, 100, 250);
}

TEST(modify_reduce_keeps_priority) {
    Fixture f;
    f.rest(limit(1, Side::Buy, 100, 500));
    f.rest(limit(2, Side::Buy, 100, 500));
    const SubmitResult r = f.engine.modify(1, 100, 300, f.trades);
    CHECK(r.status == SubmitStatus::Rested);
    CHECK_EQ(r.resting, Quantity{300});
    CHECK_EQ(f.book.front(*f.book.best_bid()).id, OrderId{1});
    CHECK_EQ(f.book.front(*f.book.best_bid()).quantity, Quantity{300});
    CHECK_EQ(f.book.best_bid()->total_quantity, std::uint64_t{800});
}

TEST(modify_increase_loses_priority) {
    Fixture f;
    f.rest(limit(1, Side::Buy, 100, 500));
    f.rest(limit(2, Side::Buy, 100, 500));
    CHECK(f.engine.modify(1, 100, 600, f.trades).status == SubmitStatus::Rested);
    CHECK_EQ(f.book.front(*f.book.best_bid()).id, OrderId{2});
    std::vector<OrderId> ids;
    f.book.for_each_order(*f.book.best_bid(), [&](const OrderNode& n) { ids.push_back(n.id); });
    CHECK((ids == std::vector<OrderId>{2, 1}));
    CHECK_EQ(f.book.best_bid()->total_quantity, std::uint64_t{1100});
}

TEST(modify_price_moves_order_and_may_trade) {
    Fixture f;
    f.rest(limit(1, Side::Buy, 100, 500));
    f.rest(limit(2, Side::Sell, 105, 200));
    const SubmitResult r = f.engine.modify(1, 101, 300, f.trades);  // re-price, no cross
    CHECK(r.status == SubmitStatus::Rested);
    CHECK_EQ(f.book.best_bid_price(), Price{101});
    CHECK_EQ(f.book.front(*f.book.best_bid()).quantity, Quantity{300});
    CHECK(f.book.level(Side::Buy, 100) == nullptr);

    const SubmitResult crossed = f.engine.modify(1, 105, 300, f.trades);  // now crosses the ask
    CHECK(crossed.status == SubmitStatus::PartiallyFilled);
    REQUIRE(f.trades.size() == 1);
    check_trade(f.trades[0], 1, 2, 105, 200);
    CHECK(!f.book.has_asks());
    CHECK_EQ(f.book.best_bid_price(), Price{105});
    CHECK_EQ(f.book.front(*f.book.best_bid()).quantity, Quantity{100});
}

TEST(modify_rejections_leave_book_untouched) {
    Fixture f;
    f.rest(limit(1, Side::Buy, 100, 500));
    CHECK(f.engine.modify(42, 100, 10, f.trades).reason == RejectReason::UnknownOrder);
    CHECK(f.engine.modify(1, 100, 0, f.trades).reason == RejectReason::ZeroQuantity);
    CHECK(f.engine.modify(1, 5000, 10, f.trades).reason == RejectReason::PriceOutOfRange);
    CHECK(f.book.contains(1));
    CHECK_EQ(f.book.front(*f.book.best_bid()).quantity, Quantity{500});
}

TEST(callback_overload_delivers_trades_without_vector) {
    Fixture f;
    f.rest(limit(1, Side::Sell, 100, 100));
    f.rest(limit(2, Side::Sell, 100, 100));
    int n = 0;
    Quantity total = 0;
    f.engine.submit(limit(3, Side::Buy, 100, 150), [&](const Trade& t) { ++n; total += t.quantity; });
    CHECK_EQ(n, 2);
    CHECK_EQ(total, Quantity{150});
}

TEST(large_sweep_across_many_levels) {
    OrderBook book(1, 100'000);
    MatchingEngine engine(book);
    std::vector<Trade> trades;
    for (Price p = 50'000; p < 51'000; ++p) REQUIRE(book.add(limit(static_cast<OrderId>(p), Side::Sell, p, 1)) == RejectReason::None);
    const SubmitResult r = engine.submit(market(1, Side::Buy, 1'000), trades);
    CHECK(r.status == SubmitStatus::Filled);
    CHECK_EQ(trades.size(), std::size_t{1'000});
    CHECK_EQ(trades.front().price, Price{50'000});
    CHECK_EQ(trades.back().price, Price{50'999});
    CHECK(book.empty());
}

int main() { return lob::test::run_all(); }
