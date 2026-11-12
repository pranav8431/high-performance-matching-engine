#pragma once

// OrderId -> SlotIndex hash map used for O(1) cancel / modify lookups.
//
// This replaces std::unordered_map, which allocates a heap node per entry and
// chases a pointer per lookup. This table is:
//   * open addressing with linear probing (one contiguous array, cache friendly),
//   * power-of-two capacity with Fibonacci hashing (multiply, take high bits),
//   * max load factor 0.5, grows by doubling,
//   * deletion by backward shift, so there are no tombstones and lookups never
//     degrade over time no matter how many orders have been cancelled.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "lob/types.hpp"

namespace lob {

class OrderIndex {
public:
    explicit OrderIndex(std::size_t expected_entries = 1024) {
        rehash(next_pow2(expected_entries * 2 < 16 ? 16 : expected_entries * 2));
    }

    // Returns false (and leaves the table unchanged) if `key` already exists.
    bool insert(OrderId key, SlotIndex value) {
        if ((size_ + 1) * 2 > table_.size()) {
            rehash(table_.size() * 2);
        }
        std::size_t i = home(key);
        while (table_[i].value != kInvalidSlot) {
            if (table_[i].key == key) {
                return false;
            }
            i = (i + 1) & mask_;
        }
        table_[i] = Entry{key, value};
        ++size_;
        return true;
    }

    // Returns kInvalidSlot if absent.
    SlotIndex find(OrderId key) const noexcept {
        std::size_t i = home(key);
        while (table_[i].value != kInvalidSlot) {
            if (table_[i].key == key) {
                return table_[i].value;
            }
            i = (i + 1) & mask_;
        }
        return kInvalidSlot;
    }

    bool contains(OrderId key) const noexcept { return find(key) != kInvalidSlot; }

    bool erase(OrderId key) noexcept {
        std::size_t i = home(key);
        while (true) {
            if (table_[i].value == kInvalidSlot) {
                return false;
            }
            if (table_[i].key == key) {
                break;
            }
            i = (i + 1) & mask_;
        }

        // Backward-shift deletion: pull later entries of the same probe
        // sequence back into the hole so no tombstone is needed.
        std::size_t j = i;
        while (true) {
            j = (j + 1) & mask_;
            if (table_[j].value == kInvalidSlot) {
                break;
            }
            const std::size_t h = home(table_[j].key);
            // Entry j may stay only if its home lies in the cyclic range (i, j].
            const bool stays = (i <= j) ? (i < h && h <= j) : (i < h || h <= j);
            if (!stays) {
                table_[i] = table_[j];
                i = j;
            }
        }
        table_[i].value = kInvalidSlot;
        --size_;
        return true;
    }

    void reserve(std::size_t expected_entries) {
        const std::size_t needed = next_pow2(expected_entries * 2);
        if (needed > table_.size()) {
            rehash(needed);
        }
    }

    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return table_.size(); }
    bool empty() const noexcept { return size_ == 0; }

private:
    struct Entry {
        OrderId   key;
        SlotIndex value;  // kInvalidSlot marks an empty bucket
    };

    static std::size_t next_pow2(std::size_t n) noexcept {
        std::size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    std::size_t home(OrderId key) const noexcept {
        // Fibonacci hashing: the golden-ratio multiplier spreads sequential ids
        // across the table, and the high bits are the well-mixed ones.
        return static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ULL) >> shift_);
    }

    void rehash(std::size_t new_capacity) {
        assert((new_capacity & (new_capacity - 1)) == 0);
        std::vector<Entry> old = std::move(table_);
        table_.assign(new_capacity, Entry{0, kInvalidSlot});
        mask_  = new_capacity - 1;
        shift_ = 64u - log2_of_pow2(new_capacity);
        size_  = 0;
        for (const Entry& e : old) {
            if (e.value != kInvalidSlot) {
                std::size_t i = home(e.key);
                while (table_[i].value != kInvalidSlot) i = (i + 1) & mask_;
                table_[i] = e;
                ++size_;
            }
        }
    }

    static unsigned log2_of_pow2(std::size_t v) noexcept {
        unsigned r = 0;
        while ((std::size_t{1} << r) < v) ++r;
        return r;
    }

    std::vector<Entry> table_;
    std::size_t mask_  = 0;
    unsigned    shift_ = 64;
    std::size_t size_  = 0;
};

}  // namespace lob
