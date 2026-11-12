#pragma once

// One side (bids or asks) of the order book.
//
// Price levels are stored in a dense array indexed by price tick, which gives
// O(1) access to any level with no tree walk and no hashing. To avoid a
// branch per side, prices are mapped so that the *best* price always lands at
// the lowest index:
//     bids: index = max_price - price     (highest bid  -> index 0)
//     asks: index = price   - min_price   (lowest  ask  -> index 0)
//
// A bitmap with one bit per level marks non-empty levels, so when the best
// level is emptied the next best is found with a count-trailing-zeros scan
// instead of walking the array one level at a time.

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "lob/order_pool.hpp"
#include "lob/price_level.hpp"

namespace lob {

class BookSide {
public:
    static constexpr std::size_t kNoLevel = std::numeric_limits<std::size_t>::max();

    BookSide(Side side, Price min_price, Price max_price)
        : side_(side), min_price_(min_price), max_price_(max_price),
          levels_(static_cast<std::size_t>(max_price - min_price + 1)),
          bitmap_((levels_.size() + 63) / 64, 0) {
        assert(max_price >= min_price);
        for (std::size_t i = 0; i < levels_.size(); ++i) {
            levels_[i].price = price_of(i);
        }
    }

    Side  side() const noexcept { return side_; }
    Price min_price() const noexcept { return min_price_; }
    Price max_price() const noexcept { return max_price_; }

    bool in_range(Price p) const noexcept { return p >= min_price_ && p <= max_price_; }

    std::size_t index_of(Price p) const noexcept {
        assert(in_range(p));
        return side_ == Side::Buy ? static_cast<std::size_t>(max_price_ - p)
                                  : static_cast<std::size_t>(p - min_price_);
    }

    Price price_of(std::size_t idx) const noexcept {
        return side_ == Side::Buy ? max_price_ - static_cast<Price>(idx)
                                  : min_price_ + static_cast<Price>(idx);
    }

    bool empty() const noexcept { return best_idx_ == kNoLevel; }

    std::size_t best_index() const noexcept { return best_idx_; }

    PriceLevel* best() noexcept {
        return best_idx_ == kNoLevel ? nullptr : &levels_[best_idx_];
    }

    const PriceLevel* best() const noexcept {
        return best_idx_ == kNoLevel ? nullptr : &levels_[best_idx_];
    }

    PriceLevel& level_at(std::size_t idx) noexcept { return levels_[idx]; }
    const PriceLevel& level_at(std::size_t idx) const noexcept { return levels_[idx]; }

    // Returns the level for `price` if it has resting orders, else nullptr.
    const PriceLevel* level(Price price) const noexcept {
        if (!in_range(price)) return nullptr;
        const PriceLevel& lvl = levels_[index_of(price)];
        return lvl.empty() ? nullptr : &lvl;
    }

    std::size_t nonempty_levels() const noexcept { return nonempty_levels_; }

    // Next non-empty level strictly after `idx` in priority order, or kNoLevel.
    std::size_t next_nonempty(std::size_t idx) const noexcept {
        return find_next_set(idx + 1);
    }

    // Append `slot` to the back of the FIFO at level `idx`.
    void link_back(std::size_t idx, SlotIndex slot, OrderPool& pool) noexcept {
        PriceLevel& lvl  = levels_[idx];
        OrderNode&  node = pool[slot];
        node.prev = lvl.tail;
        node.next = kInvalidSlot;
        if (lvl.tail != kInvalidSlot) {
            pool[lvl.tail].next = slot;
        } else {
            lvl.head = slot;
            set_bit(idx);
            ++nonempty_levels_;
            if (idx < best_idx_) best_idx_ = idx;
        }
        lvl.tail = slot;
        lvl.total_quantity += node.quantity;
        ++lvl.order_count;
    }

    // Remove `slot` from level `idx` (any position in the FIFO).
    void unlink(std::size_t idx, SlotIndex slot, OrderPool& pool) noexcept {
        PriceLevel& lvl  = levels_[idx];
        OrderNode&  node = pool[slot];
        if (node.prev != kInvalidSlot) pool[node.prev].next = node.next; else lvl.head = node.next;
        if (node.next != kInvalidSlot) pool[node.next].prev = node.prev; else lvl.tail = node.prev;
        lvl.total_quantity -= node.quantity;
        --lvl.order_count;
        if (lvl.head == kInvalidSlot) {
            clear_bit(idx);
            --nonempty_levels_;
            if (idx == best_idx_) best_idx_ = find_next_set(idx + 1);
        }
    }

private:
    void set_bit(std::size_t idx) noexcept { bitmap_[idx >> 6] |= (std::uint64_t{1} << (idx & 63)); }
    void clear_bit(std::size_t idx) noexcept { bitmap_[idx >> 6] &= ~(std::uint64_t{1} << (idx & 63)); }

    // Lowest set bit at position >= from, or kNoLevel.
    std::size_t find_next_set(std::size_t from) const noexcept {
        std::size_t w = from >> 6;
        if (w >= bitmap_.size()) return kNoLevel;
        std::uint64_t word = bitmap_[w] & (~std::uint64_t{0} << (from & 63));
        while (true) {
            if (word != 0) {
                return (w << 6) + static_cast<std::size_t>(std::countr_zero(word));
            }
            if (++w == bitmap_.size()) return kNoLevel;
            word = bitmap_[w];
        }
    }

    Side  side_;
    Price min_price_;
    Price max_price_;
    std::vector<PriceLevel>    levels_;
    std::vector<std::uint64_t> bitmap_;
    std::size_t best_idx_        = kNoLevel;
    std::size_t nonempty_levels_ = 0;
};

}  // namespace lob
