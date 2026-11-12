#pragma once

// Slab allocator for resting orders.
//
// Orders live in one contiguous std::vector<OrderNode> and are addressed by a
// 32-bit SlotIndex rather than a pointer. Benefits:
//   * zero heap allocations in steady state (freed slots are recycled via an
//     intrusive free list threaded through `next`),
//   * indices stay valid when the vector grows, unlike pointers/iterators,
//   * 32-bit links halve the size of the intrusive list compared to pointers,
//     keeping a node at exactly 32 bytes (two nodes per cache line).

#include <cassert>
#include <cstddef>
#include <vector>

#include "lob/types.hpp"

namespace lob {

struct OrderNode {
    OrderId   id;
    Price     price;
    Quantity  quantity;  // remaining quantity
    Side      side;
    SlotIndex prev;      // previous order at the same price level (FIFO)
    SlotIndex next;      // next order at the same price level, or free-list link
};

static_assert(sizeof(OrderNode) == 32, "OrderNode must stay 32 bytes");

class OrderPool {
public:
    explicit OrderPool(std::size_t initial_capacity = 1024) {
        nodes_.reserve(initial_capacity);
    }

    SlotIndex allocate() {
        ++live_;
        if (free_head_ != kInvalidSlot) {
            const SlotIndex slot = free_head_;
            free_head_ = nodes_[slot].next;
            return slot;
        }
        nodes_.emplace_back();
        return static_cast<SlotIndex>(nodes_.size() - 1);
    }

    void release(SlotIndex slot) noexcept {
        assert(slot < nodes_.size());
        nodes_[slot].next = free_head_;
        free_head_ = slot;
        --live_;
    }

    OrderNode& operator[](SlotIndex slot) noexcept {
        assert(slot < nodes_.size());
        return nodes_[slot];
    }

    const OrderNode& operator[](SlotIndex slot) const noexcept {
        assert(slot < nodes_.size());
        return nodes_[slot];
    }

    std::size_t live() const noexcept { return live_; }
    std::size_t capacity() const noexcept { return nodes_.size(); }

private:
    std::vector<OrderNode> nodes_;
    SlotIndex   free_head_ = kInvalidSlot;
    std::size_t live_      = 0;
};

}  // namespace lob
