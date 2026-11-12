// Micro-benchmark of the OrderId -> slot index: custom open-addressing table
// vs std::unordered_map. Same keys, same operation order.

#include <cstdio>
#include <random>
#include <unordered_map>
#include <vector>

#include "bench_common.hpp"
#include "lob/order_index.hpp"

using namespace lob;
using namespace lob::bench;

template <typename Insert, typename Find, typename Erase>
static void run(const char* name, const std::vector<OrderId>& keys, const std::vector<OrderId>& lookups,
                Insert insert, Find find, Erase erase) {
    std::printf("  %s\n", name);
    auto t = Clock::now();
    for (std::size_t i = 0; i < keys.size(); ++i) insert(keys[i], static_cast<SlotIndex>(i));
    print_rate("insert", keys.size(), seconds_since(t));

    std::uint64_t found = 0;
    t = Clock::now();
    for (OrderId k : lookups) found += find(k) ? 1 : 0;
    print_rate("find (50% hit rate)", lookups.size(), seconds_since(t));

    t = Clock::now();
    for (OrderId k : keys) erase(k);
    print_rate("erase", keys.size(), seconds_since(t));
    std::printf("  %-34s %14s\n\n", "  lookups that hit", with_commas(found).c_str());
}

int main(int argc, char** argv) {
    const std::size_t n = arg_size(argc, argv, 1, 10'000'000);
    print_build_info();

    // Mostly sequential ids with gaps, like a real exchange feed.
    std::vector<OrderId> keys(n);
    std::mt19937_64 rng(1);
    OrderId next = 1;
    for (OrderId& k : keys) { next += 1 + (rng() % 3); k = next; }
    std::vector<OrderId> lookups(n);
    for (std::size_t i = 0; i < n; ++i) lookups[i] = (i % 2 == 0) ? keys[rng() % n] : next + 1 + static_cast<OrderId>(rng() % n);

    std::printf("\nOrder index: %s keys\n\n", with_commas(n).c_str());

    {
        OrderIndex idx(n);
        run("lob::OrderIndex (open addressing, backward-shift delete)", keys, lookups,
            [&](OrderId k, SlotIndex v) { idx.insert(k, v); },
            [&](OrderId k) { return idx.find(k) != kInvalidSlot; },
            [&](OrderId k) { idx.erase(k); });
    }
    {
        std::unordered_map<OrderId, SlotIndex> m;
        m.reserve(n);
        run("std::unordered_map (reserved)", keys, lookups,
            [&](OrderId k, SlotIndex v) { m.emplace(k, v); },
            [&](OrderId k) { return m.find(k) != m.end(); },
            [&](OrderId k) { m.erase(k); });
    }
    return 0;
}
