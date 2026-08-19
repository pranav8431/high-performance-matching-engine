#include <iostream>

#include "matching_engine.hpp"

int main() {
    OrderBook book;
    MatchingEngine engine(book);

    // Resting sells
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

    // Incoming buy
    Order buy_order{
        3,
        Side::BUY,
        OrderType::LIMIT,
        101,
        700
    };

    auto trades = engine.process_order(buy_order);

    for (const auto& trade : trades) {
        std::cout << "Trade\n";
        std::cout << "Buyer: " << trade.buy_order_id << '\n';
        std::cout << "Seller: " << trade.sell_order_id << '\n';
        std::cout << "Price: " << trade.price << '\n';
        std::cout << "Quantity: " << trade.quantity << '\n';
        std::cout << "----------------\n";
    }

    return 0;
}