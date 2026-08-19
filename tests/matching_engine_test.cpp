#include <cassert>
#include <iostream>
#include "matching_engine.hpp"

void test_partial_fill(){
    OrderBook book;
    MatchingEngine engine(book);
    book.add_order({
        1,
        Side :: SELL,
        OrderType ::LIMIT,
        100,
        300
    });
    engine.process_order({
        2,
        Side::BUY,
        OrderType :: LIMIT,
        100,
        500
    });
    assert(!book.has_asks());
}


void test_multiple_price_level(){
    OrderBook book;
    MatchingEngine engine(book);
    book.add_order({
        1,
        Side::SELL,
        OrderType::LIMIT,
        100,
        500
    });

    book.add_order({
        2,
        Side::SELL,
        OrderType::LIMIT,
        101,
        300
    });

    engine.process_order({
        3,
        Side::BUY,
        OrderType::LIMIT,
        101,
        700
    });

    assert(book.has_asks());
    assert(book.best_ask() == 101);
    assert(book.best_ask_level().front().quantity == 100);
}


void test_complete_fill() {
    OrderBook book;
    MatchingEngine engine(book);

    book.add_order({
        1,
        Side::SELL,
        OrderType::LIMIT,
        100,
        500
    });

    engine.process_order({
        2,
        Side::BUY,
        OrderType::LIMIT,
        100,
        500
    });

    assert(!book.has_asks());
}


void test_price_does_not_cross() {
    OrderBook book;
    MatchingEngine engine(book);

    book.add_order({
        1,
        Side::SELL,
        OrderType::LIMIT,
        100,
        500
    });

    engine.process_order({
        2,
        Side::BUY,
        OrderType::LIMIT,
        99,
        300
    });

    assert(book.has_asks());
    assert(book.best_ask() == 100);
    assert(book.best_ask_level().front().quantity == 500);

    assert(book.has_bids());
    assert(book.best_bid() == 99);
    assert(book.best_bid_level().front().quantity == 300);
}





void test_cancellation() {
    OrderBook book;

    book.add_order({
        1,
        Side::SELL,
        OrderType::LIMIT,
        100,
        500
    });

    assert(book.cancel_order(1));
    assert(!book.has_asks());

    assert(!book.cancel_order(999));
}




void test_multiple_price_levels() {
    OrderBook book;
    MatchingEngine engine(book);

    book.add_order({
        1,
        Side::SELL,
        OrderType::LIMIT,
        100,
        500
    });

    book.add_order({
        2,
        Side::SELL,
        OrderType::LIMIT,
        101,
        300
    });

    engine.process_order({
        3,
        Side::BUY,
        OrderType::LIMIT,
        101,
        700
    });

    assert(book.has_asks());
    assert(book.best_ask() == 101);
    assert(book.best_ask_level().front().quantity == 100);
}



int main() {
    test_partial_fill();
    test_complete_fill();
    test_price_does_not_cross();
    test_multiple_price_levels();
    test_cancellation();

    std::cout << "All tests passed.\n";

    return 0;
}