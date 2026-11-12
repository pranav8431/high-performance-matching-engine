#include <random>
#include <unordered_map>

#include "lob/order_index.hpp"
#include "test_framework.hpp"

using namespace lob;

TEST(index_insert_find_erase) {
    OrderIndex idx(4);
    CHECK(idx.empty());
    CHECK(idx.insert(10, 0));
    CHECK(idx.insert(20, 1));
    CHECK(idx.insert(30, 2));
    CHECK(!idx.insert(20, 99));  // duplicate rejected
    CHECK_EQ(idx.size(), std::size_t{3});
    CHECK_EQ(idx.find(10), SlotIndex{0});
    CHECK_EQ(idx.find(20), SlotIndex{1});
    CHECK_EQ(idx.find(30), SlotIndex{2});
    CHECK_EQ(idx.find(40), kInvalidSlot);
    CHECK(idx.erase(20));
    CHECK(!idx.erase(20));
    CHECK_EQ(idx.find(20), kInvalidSlot);
    CHECK_EQ(idx.find(10), SlotIndex{0});
    CHECK_EQ(idx.find(30), SlotIndex{2});
    CHECK_EQ(idx.size(), std::size_t{2});
}

TEST(index_grows_and_keeps_entries) {
    OrderIndex idx(2);
    const std::size_t n = 100'000;
    for (OrderId i = 1; i <= n; ++i) CHECK(idx.insert(i, static_cast<SlotIndex>(i * 7)));
    CHECK_EQ(idx.size(), n);
    CHECK(idx.capacity() >= 2 * n);  // load factor <= 0.5
    for (OrderId i = 1; i <= n; ++i) CHECK_EQ(idx.find(i), static_cast<SlotIndex>(i * 7));
}

TEST(index_backward_shift_keeps_probe_chains_intact) {
    // Keys that collide in a tiny table exercise the backward-shift path.
    OrderIndex idx(8);
    std::vector<OrderId> keys;
    for (OrderId k = 1; k <= 7; ++k) keys.push_back(k * 1'000'003ULL);
    for (std::size_t i = 0; i < keys.size(); ++i) CHECK(idx.insert(keys[i], static_cast<SlotIndex>(i)));
    // Delete every other key, then verify all remaining keys are still reachable.
    for (std::size_t i = 0; i < keys.size(); i += 2) CHECK(idx.erase(keys[i]));
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i % 2 == 0) CHECK_EQ(idx.find(keys[i]), kInvalidSlot);
        else            CHECK_EQ(idx.find(keys[i]), static_cast<SlotIndex>(i));
    }
}

TEST(index_matches_std_unordered_map_under_random_ops) {
    OrderIndex idx(16);
    std::unordered_map<OrderId, SlotIndex> ref;
    std::mt19937_64 rng(7);
    std::uniform_int_distribution<OrderId> key(1, 5000);  // small key space forces reuse
    std::uniform_int_distribution<int> op(0, 2);

    for (int i = 0; i < 300'000; ++i) {
        const OrderId k = key(rng);
        switch (op(rng)) {
            case 0: {
                const SlotIndex v = static_cast<SlotIndex>(i);
                const bool inserted = idx.insert(k, v);
                const bool ref_inserted = ref.emplace(k, v).second;
                CHECK_EQ(inserted, ref_inserted);
                break;
            }
            case 1: {
                const bool erased = idx.erase(k);
                CHECK_EQ(erased, ref.erase(k) == 1);
                break;
            }
            default: {
                auto it = ref.find(k);
                CHECK_EQ(idx.find(k), it == ref.end() ? kInvalidSlot : it->second);
                break;
            }
        }
        CHECK_EQ(idx.size(), ref.size());
    }
    for (const auto& [k, v] : ref) CHECK_EQ(idx.find(k), v);
}

int main() { return lob::test::run_all(); }
