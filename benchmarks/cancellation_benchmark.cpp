#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#include "order_book.hpp"

int main() {
    constexpr std::size_t NUM_ORDERS = 10'000'000;
    constexpr std::size_t NUM_CANCELLATIONS = 1'000'000;

    OrderBook book;

    // Generate order IDs before timing.
    std::vector<uint64_t> order_ids;
    order_ids.reserve(NUM_ORDERS);

    for (uint64_t i = 1; i <= NUM_ORDERS; ++i) {
        order_ids.push_back(i);

        book.add_order({
            i,
            Side::BUY,
            OrderType::LIMIT,
            100,
            100
        });
    }

    // Generate cancellation IDs before timing.
    std::mt19937_64 rng(42);

    std::vector<uint64_t> cancellation_ids;
    cancellation_ids.reserve(NUM_CANCELLATIONS);

    std::uniform_int_distribution<uint64_t> id_dist(
        1,
        NUM_ORDERS
    );

    for (std::size_t i = 0; i < NUM_CANCELLATIONS; ++i) {
        cancellation_ids.push_back(id_dist(rng));
    }

    std::size_t successful_cancellations = 0;

    // Measure ONLY cancellation.
    auto start = std::chrono::steady_clock::now();

    for (uint64_t id : cancellation_ids) {
        if (book.cancel_order(id)) {
            ++successful_cancellations;
        }
    }

    auto end = std::chrono::steady_clock::now();

    std::chrono::duration<double> elapsed = end - start;

    double seconds = elapsed.count();

    double cancellations_per_second =
        static_cast<double>(NUM_CANCELLATIONS) / seconds;

    std::cout << "Orders initially     : "
              << NUM_ORDERS << '\n';

    std::cout << "Cancellation requests: "
              << NUM_CANCELLATIONS << '\n';

    std::cout << "Successful cancels   : "
              << successful_cancellations << '\n';

    std::cout << "Time taken            : "
              << seconds << " seconds\n";

    std::cout << "Cancellation rate     : "
              << cancellations_per_second / 1'000'000.0
              << " million cancellations/sec\n";

    return 0;
}