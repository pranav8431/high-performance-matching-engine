#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#include "matching_engine.hpp"

int main() {
    constexpr std::size_t NUM_ORDERS = 50'000'000;

    std::vector<Order> orders;
    orders.reserve(NUM_ORDERS);

    // Generate the workload BEFORE starting the timer.
    std::mt19937_64 rng(42);

    std::uniform_int_distribution<int64_t> price_dist(95, 105);
    std::uniform_int_distribution<uint64_t> quantity_dist(1, 100);

    for (uint64_t i = 1; i <= NUM_ORDERS; ++i) {
        Order order{
            i,
            (i % 2 == 0) ? Side::BUY : Side::SELL,
            OrderType::LIMIT,
            price_dist(rng),
            quantity_dist(rng)
        };

        orders.push_back(order);
    }

    OrderBook book;
    MatchingEngine engine(book);

    // Start timing ONLY the matching engine.
    auto start = std::chrono::steady_clock::now();

    for (const auto& order : orders) {
        engine.process_order(order);
    }

    auto end = std::chrono::steady_clock::now();

    std::chrono::duration<double> elapsed = end - start;

    double seconds = elapsed.count();

    double orders_per_second =
    static_cast<double>(NUM_ORDERS) / seconds;
    std::cout << "\n\n-----------------------------------------------------------------------------\n\n";
    std::cout << "Orders processed : " << NUM_ORDERS << '\n';
    std::cout << "Time taken       : " << seconds << " seconds\n";
    std::cout << "Throughput       : " << orders_per_second / 1'000'000.0 << " million orders/sec\n";
    std::cout << "\n\n-----------------------------------------------------------------------------\n\n";
    return 0;
}