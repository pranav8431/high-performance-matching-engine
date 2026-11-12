#include <vector>

#include "lob/order_book.hpp"
#include "test_framework.hpp"

using namespace lob;

namespace {
Order limit(OrderId id, Side side, Price price, Quantity qty) {
    return Order{id, price, qty, side, OrderType::Limit, TimeInForce::GTC};
}

std::vector<OrderId> ids_at(const OrderBook& book, Side side, Price price) {
    std::vector<OrderId> out;
    if (const PriceLevel* lvl = book.level(side, price)) {
        book.for_each_order(*lvl, [&](const OrderNode& n) { out.push_back(n.id); });
    }
    return out;
}
}  // namespace

TEST(book_starts_empty) {
    OrderBook book(1, 1000);
    CHECK(book.empty());
    CHECK(!book.has_bids());
    CHECK(!book.has_asks());
    CHECK(book.best_bid() == nullptr);
    CHECK(book.best_ask() == nullptr);
    CHECK_EQ(book.order_count(), std::size_t{0});
}

TEST(book_add_tracks_best_and_level_totals) {
    OrderBook book(1, 1000);
    CHECK(book.add(limit(1, Side::Buy, 100, 10)) == RejectReason::None);
    CHECK(book.add(limit(2, Side::Buy, 102, 20)) == RejectReason::None);
    CHECK(book.add(limit(3, Side::Buy, 101, 30)) == RejectReason::None);
    CHECK(book.add(limit(4, Side::Sell, 105, 40)) == RejectReason::None);
    CHECK(book.add(limit(5, Side::Sell, 103, 50)) == RejectReason::None);
    CHECK(book.add(limit(6, Side::Sell, 103, 60)) == RejectReason::None);

    REQUIRE(book.has_bids() && book.has_asks());
    CHECK_EQ(book.best_bid_price(), Price{102});
    CHECK_EQ(book.best_ask_price(), Price{103});
    CHECK_EQ(book.best_ask()->total_quantity, std::uint64_t{110});
    CHECK_EQ(book.best_ask()->order_count, std::uint32_t{2});
    CHECK_EQ(book.front(*book.best_ask()).id, OrderId{5});
    CHECK_EQ(book.level_count(Side::Buy), std::size_t{3});
    CHECK_EQ(book.level_count(Side::Sell), std::size_t{2});
    CHECK_EQ(book.order_count(), std::size_t{6});
}

TEST(book_rejects_bad_orders) {
    OrderBook book(10, 20);
    CHECK(book.add(limit(1, Side::Buy, 15, 0)) == RejectReason::ZeroQuantity);
    CHECK(book.add(limit(1, Side::Buy, 9, 5)) == RejectReason::PriceOutOfRange);
    CHECK(book.add(limit(1, Side::Buy, 21, 5)) == RejectReason::PriceOutOfRange);
    CHECK(book.add(limit(1, Side::Buy, 10, 5)) == RejectReason::None);
    CHECK(book.add(limit(1, Side::Sell, 20, 5)) == RejectReason::DuplicateId);
    CHECK(book.add(limit(2, Side::Sell, 20, 5)) == RejectReason::None);
    CHECK_EQ(book.order_count(), std::size_t{2});
}

TEST(book_cancel_any_position_preserves_fifo) {
    OrderBook book(1, 1000);
    for (OrderId id = 1; id <= 5; ++id) book.add(limit(id, Side::Sell, 100, 10));
    CHECK((ids_at(book, Side::Sell, 100) == std::vector<OrderId>{1, 2, 3, 4, 5}));

    CHECK(book.cancel(3));  // middle
    CHECK((ids_at(book, Side::Sell, 100) == std::vector<OrderId>{1, 2, 4, 5}));
    CHECK(book.cancel(1));  // head
    CHECK((ids_at(book, Side::Sell, 100) == std::vector<OrderId>{2, 4, 5}));
    CHECK(book.cancel(5));  // tail
    CHECK((ids_at(book, Side::Sell, 100) == std::vector<OrderId>{2, 4}));
    CHECK_EQ(book.best_ask()->total_quantity, std::uint64_t{20});
    CHECK(!book.cancel(3));   // already gone
    CHECK(!book.cancel(99));  // never existed

    CHECK(book.cancel(2));
    CHECK(book.cancel(4));
    CHECK(!book.has_asks());
    CHECK_EQ(book.level_count(Side::Sell), std::size_t{0});
    CHECK_EQ(book.order_count(), std::size_t{0});
}

TEST(book_best_moves_to_next_level_when_best_is_emptied) {
    OrderBook book(1, 100'000);
    book.add(limit(1, Side::Buy, 50'000, 1));
    book.add(limit(2, Side::Buy, 49'000, 1));   // far away: different bitmap word
    book.add(limit(3, Side::Buy, 10, 1));       // very far away
    CHECK_EQ(book.best_bid_price(), Price{50'000});
    book.cancel(1);
    CHECK_EQ(book.best_bid_price(), Price{49'000});
    book.cancel(2);
    CHECK_EQ(book.best_bid_price(), Price{10});
    book.cancel(3);
    CHECK(!book.has_bids());

    book.add(limit(4, Side::Sell, 70'000, 1));
    book.add(limit(5, Side::Sell, 99'999, 1));
    book.add(limit(6, Side::Sell, 99'998, 1));
    CHECK_EQ(book.best_ask_price(), Price{70'000});
    book.cancel(4);
    CHECK_EQ(book.best_ask_price(), Price{99'998});
    book.cancel(6);
    CHECK_EQ(book.best_ask_price(), Price{99'999});
}

TEST(book_reduce_keeps_priority) {
    OrderBook book(1, 1000);
    book.add(limit(1, Side::Buy, 100, 50));
    book.add(limit(2, Side::Buy, 100, 50));
    CHECK(book.reduce(1, 20));
    CHECK_EQ(book.front(*book.best_bid()).id, OrderId{1});
    CHECK_EQ(book.front(*book.best_bid()).quantity, Quantity{20});
    CHECK_EQ(book.best_bid()->total_quantity, std::uint64_t{70});
    CHECK(!book.reduce(1, 20));   // not a reduction
    CHECK(!book.reduce(1, 25));   // increase not allowed
    CHECK(!book.reduce(1, 0));
    CHECK(!book.reduce(42, 1));
}

TEST(book_fill_front_removes_fully_filled_orders) {
    OrderBook book(1, 1000);
    book.add(limit(1, Side::Sell, 100, 30));
    book.add(limit(2, Side::Sell, 100, 30));
    book.fill_front(Side::Sell, 10);
    CHECK_EQ(book.front(*book.best_ask()).id, OrderId{1});
    CHECK_EQ(book.front(*book.best_ask()).quantity, Quantity{20});
    book.fill_front(Side::Sell, 20);
    CHECK_EQ(book.front(*book.best_ask()).id, OrderId{2});
    CHECK(!book.contains(1));
    CHECK_EQ(book.best_ask()->total_quantity, std::uint64_t{30});
    book.fill_front(Side::Sell, 30);
    CHECK(!book.has_asks());
    CHECK_EQ(book.order_count(), std::size_t{0});
}

TEST(book_for_each_level_iterates_in_priority_order) {
    OrderBook book(1, 1000);
    for (Price p : {105, 101, 103}) book.add(limit(static_cast<OrderId>(p), Side::Buy, p, 1));
    for (Price p : {205, 201, 203}) book.add(limit(static_cast<OrderId>(p), Side::Sell, p, 1));
    std::vector<Price> bids, asks;
    book.for_each_level(Side::Buy,  [&](const PriceLevel& l) { bids.push_back(l.price); return true; });
    book.for_each_level(Side::Sell, [&](const PriceLevel& l) { asks.push_back(l.price); return true; });
    CHECK((bids == std::vector<Price>{105, 103, 101}));
    CHECK((asks == std::vector<Price>{201, 203, 205}));

    int visited = 0;
    book.for_each_level(Side::Buy, [&](const PriceLevel&) { return ++visited < 2; });
    CHECK_EQ(visited, 2);
}

TEST(book_slot_reuse_after_many_cycles) {
    OrderBook book(1, 100, 16);
    for (int round = 0; round < 1000; ++round) {
        for (OrderId id = 1; id <= 16; ++id) CHECK(book.add(limit(id, Side::Buy, 50, 1)) == RejectReason::None);
        for (OrderId id = 1; id <= 16; ++id) CHECK(book.cancel(id));
    }
    CHECK(book.empty());
}

int main() { return lob::test::run_all(); }
