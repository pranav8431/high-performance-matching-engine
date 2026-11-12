#pragma once

// One price level: a FIFO queue of resting orders at the same price.
//
// The queue is an intrusive doubly linked list threaded through OrderNode
// slots in the OrderPool, so the level itself is just a 32-byte header and
// enqueue/dequeue never allocate.

#include <cstdint>

#include "lob/types.hpp"

namespace lob {

struct PriceLevel {
    Price         price          = 0;
    std::uint64_t total_quantity = 0;  // sum of remaining quantity at this price
    std::uint32_t order_count    = 0;
    SlotIndex     head           = kInvalidSlot;  // oldest order (next to trade)
    SlotIndex     tail           = kInvalidSlot;  // newest order

    bool empty() const noexcept { return head == kInvalidSlot; }
};

static_assert(sizeof(PriceLevel) <= 32, "PriceLevel should fit in half a cache line");

}  // namespace lob
