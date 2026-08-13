#include <iostream>
#include "matching_engine.hpp"

int main() {
    OrderBook book;
    MatchingEngine engine(book);

    // BUY 500 @ 100
    book.add_order({
        1,
        Side::BUY,
        OrderType::LIMIT,
        100,
        500
    });

    // BUY 300 @ 99
    book.add_order({
        2,
        Side::BUY,
        OrderType::LIMIT,
        99,
        300
    });

    // Incoming SELL 700 @ 99
    Order sell_order{
        3,
        Side::SELL,
        OrderType::LIMIT,
        99,
        700
    };

    // engine.process_order(sell_order);
    auto trades = engine.process_order(sell_order);

    std::cout << "Best Bid after matching: ";

    if (book.has_bids()) {
        std::cout << book.best_bid() << '\n';

        std::cout << "Remaining quantity at best bid: "
                  << book.best_bid_level().front().quantity
                  << '\n';
    }
    else {
        std::cout << "No bids\n";
    }

    for (const auto& trade : trades) {
        std::cout << "Trade\n";
        std::cout << "Buyer: " << trade.buy_order_id << '\n';
        std::cout << "Seller: " << trade.sell_order_id << '\n';
        std::cout << "Price: " << trade.price << '\n';
        std::cout << "Quantity: " << trade.quantity << '\n';
    }
    return 0;
}