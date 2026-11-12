#pragma once

// Passive limit order book: stores resting orders for one instrument.
//
// Responsibilities:
//   * own the OrderPool (storage), OrderIndex (id -> slot) and two BookSides,
//   * add / cancel / reduce resting orders,
//   * expose the best level of each side and the FIFO at any level so the
//     MatchingEngine can execute against it.
//
// The book itself never matches; crossing logic lives in MatchingEngine so the
// data structure stays small and independently testable.

#include <cassert>
#include <cstddef>

#include "lob/book_side.hpp"
#include "lob/order_index.hpp"
#include "lob/order_pool.hpp"
#include "lob/price_level.hpp"
#include "lob/types.hpp"

namespace lob {

class OrderBook {
public:
    // Prices outside [min_price, max_price] are rejected. Memory per side is
    // (max_price - min_price + 1) * sizeof(PriceLevel) = 32 bytes per tick.
    OrderBook(Price min_price, Price max_price, std::size_t expected_orders = 1 << 16)
        : pool_(expected_orders),
          index_(expected_orders),
          bids_(Side::Buy, min_price, max_price),
          asks_(Side::Sell, min_price, max_price) {}

    Price min_price() const noexcept { return bids_.min_price(); }
    Price max_price() const noexcept { return bids_.max_price(); }
    bool  in_range(Price p) const noexcept { return bids_.in_range(p); }

    // ---- mutation -----------------------------------------------------------

    // Rest an order at the back of its price level. Does not match.
    RejectReason add(const Order& order) {
        if (order.quantity == 0)       return RejectReason::ZeroQuantity;
        if (!in_range(order.price))    return RejectReason::PriceOutOfRange;
        if (index_.contains(order.id)) return RejectReason::DuplicateId;

        const SlotIndex slot = pool_.allocate();
        OrderNode& node = pool_[slot];
        node.id       = order.id;
        node.price    = order.price;
        node.quantity = order.quantity;
        node.side     = order.side;

        BookSide& bs = side(order.side);
        bs.link_back(bs.index_of(order.price), slot, pool_);
        index_.insert(order.id, slot);
        return RejectReason::None;
    }

    bool cancel(OrderId id) noexcept {
        const SlotIndex slot = index_.find(id);
        if (slot == kInvalidSlot) return false;
        const OrderNode& node = pool_[slot];
        BookSide& bs = side(node.side);
        bs.unlink(bs.index_of(node.price), slot, pool_);
        index_.erase(id);
        pool_.release(slot);
        return true;
    }

    // Lower the quantity of a resting order in place. Queue priority is kept.
    // Fails if the order is unknown or new_quantity is not in (0, current).
    bool reduce(OrderId id, Quantity new_quantity) noexcept {
        const SlotIndex slot = index_.find(id);
        if (slot == kInvalidSlot) return false;
        OrderNode& node = pool_[slot];
        if (new_quantity == 0 || new_quantity >= node.quantity) return false;
        BookSide& bs = side(node.side);
        PriceLevel& lvl = bs.level_at(bs.index_of(node.price));
        lvl.total_quantity -= (node.quantity - new_quantity);
        node.quantity = new_quantity;
        return true;
    }

    // Execute `quantity` against the oldest order at the best level of
    // `resting_side`. Precondition: that level exists and
    // quantity <= front order quantity. Fully filled orders are removed.
    void fill_front(Side resting_side, Quantity quantity) noexcept {
        BookSide& bs = side(resting_side);
        const std::size_t idx = bs.best_index();
        assert(idx != BookSide::kNoLevel);
        PriceLevel& lvl  = bs.level_at(idx);
        const SlotIndex slot = lvl.head;
        OrderNode& node = pool_[slot];
        assert(quantity <= node.quantity);

        node.quantity      -= quantity;
        lvl.total_quantity -= quantity;
        if (node.quantity == 0) {
            index_.erase(node.id);
            bs.unlink(idx, slot, pool_);
            pool_.release(slot);
        }
    }

    // ---- queries ------------------------------------------------------------

    bool has_bids() const noexcept { return !bids_.empty(); }
    bool has_asks() const noexcept { return !asks_.empty(); }
    bool empty() const noexcept { return !has_bids() && !has_asks(); }

    const PriceLevel* best_bid() const noexcept { return bids_.best(); }
    const PriceLevel* best_ask() const noexcept { return asks_.best(); }
    const PriceLevel* best(Side s) const noexcept { return side(s).best(); }

    // Preconditions: has_bids() / has_asks().
    Price best_bid_price() const noexcept { return bids_.best()->price; }
    Price best_ask_price() const noexcept { return asks_.best()->price; }

    // Level at an exact price, or nullptr if nothing rests there.
    const PriceLevel* level(Side s, Price price) const noexcept { return side(s).level(price); }

    const OrderNode& front(const PriceLevel& lvl) const noexcept {
        assert(!lvl.empty());
        return pool_[lvl.head];
    }

    const OrderNode* find(OrderId id) const noexcept {
        const SlotIndex slot = index_.find(id);
        return slot == kInvalidSlot ? nullptr : &pool_[slot];
    }

    bool contains(OrderId id) const noexcept { return index_.contains(id); }

    std::size_t order_count() const noexcept { return index_.size(); }
    std::size_t level_count(Side s) const noexcept { return side(s).nonempty_levels(); }
    const BookSide& book_side(Side s) const noexcept { return side(s); }

    // Visit non-empty levels of `s` from best to worst. `fn(const PriceLevel&)`
    // returns false to stop early.
    template <typename Fn>
    void for_each_level(Side s, Fn&& fn) const {
        const BookSide& bs = side(s);
        for (std::size_t idx = bs.best_index(); idx != BookSide::kNoLevel; idx = bs.next_nonempty(idx)) {
            if (!fn(bs.level_at(idx))) return;
        }
    }

    // Visit resting orders at `lvl` in queue (time) priority.
    template <typename Fn>
    void for_each_order(const PriceLevel& lvl, Fn&& fn) const {
        for (SlotIndex s = lvl.head; s != kInvalidSlot; s = pool_[s].next) {
            fn(pool_[s]);
        }
    }

private:
    BookSide&       side(Side s) noexcept       { return s == Side::Buy ? bids_ : asks_; }
    const BookSide& side(Side s) const noexcept { return s == Side::Buy ? bids_ : asks_; }

    OrderPool  pool_;
    OrderIndex index_;
    BookSide   bids_;
    BookSide   asks_;
};

}  // namespace lob
