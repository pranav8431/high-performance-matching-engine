#pragma once

// Fundamental types shared by every component of the limit order book.
//
// Design rules:
//   * No floating point anywhere in the hot path. Prices are integer ticks.
//   * Fixed-width integers so struct layouts are predictable and compact.
//   * Enums are 1 byte so they pack into padding instead of widening nodes.

#include <cstdint>
#include <limits>

namespace lob {

using OrderId   = std::uint64_t;
using Price     = std::int64_t;   // price in integer ticks
using Quantity  = std::uint32_t;  // shares / contracts (ITCH uses 32 bits too)
using SlotIndex = std::uint32_t;  // index into OrderPool; stable across growth

inline constexpr SlotIndex kInvalidSlot = std::numeric_limits<SlotIndex>::max();

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

enum class OrderType : std::uint8_t {
    Limit,   // executes at `price` or better, remainder may rest
    Market   // executes at any price, remainder is cancelled
};

enum class TimeInForce : std::uint8_t {
    GTC,  // good till cancelled: unfilled remainder rests on the book
    IOC,  // immediate or cancel: unfilled remainder is discarded
    FOK   // fill or kill: either fills completely or does nothing
};

constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

constexpr const char* to_string(Side s) noexcept {
    return s == Side::Buy ? "BUY" : "SELL";
}

constexpr const char* to_string(OrderType t) noexcept {
    return t == OrderType::Limit ? "LIMIT" : "MARKET";
}

constexpr const char* to_string(TimeInForce t) noexcept {
    switch (t) {
        case TimeInForce::GTC: return "GTC";
        case TimeInForce::IOC: return "IOC";
        case TimeInForce::FOK: return "FOK";
    }
    return "?";
}

// An incoming order as submitted to the matching engine.
struct Order {
    OrderId     id;
    Price       price;      // ignored for Market orders
    Quantity    quantity;
    Side        side;
    OrderType   type = OrderType::Limit;
    TimeInForce tif  = TimeInForce::GTC;
};

// A single execution between a resting (maker) order and an incoming (taker) order.
struct Trade {
    OrderId  buy_order_id;
    OrderId  sell_order_id;
    Price    price;      // always the resting order's price
    Quantity quantity;
    Side     aggressor;  // side of the incoming order
};

enum class RejectReason : std::uint8_t {
    None,
    DuplicateId,
    PriceOutOfRange,
    ZeroQuantity,
    InsufficientLiquidity,  // FOK that cannot be filled in full
    UnknownOrder            // cancel / modify of an id that is not resting
};

constexpr const char* to_string(RejectReason r) noexcept {
    switch (r) {
        case RejectReason::None:                  return "None";
        case RejectReason::DuplicateId:           return "DuplicateId";
        case RejectReason::PriceOutOfRange:       return "PriceOutOfRange";
        case RejectReason::ZeroQuantity:          return "ZeroQuantity";
        case RejectReason::InsufficientLiquidity: return "InsufficientLiquidity";
        case RejectReason::UnknownOrder:          return "UnknownOrder";
    }
    return "?";
}

enum class SubmitStatus : std::uint8_t {
    Rejected,         // nothing happened; see `reason`
    Filled,           // fully executed
    Rested,           // no execution; the order now rests on the book
    PartiallyFilled,  // executed some; `resting` tells whether the rest rested or was cancelled
    Cancelled         // no execution and the remainder was discarded (IOC / Market)
};

constexpr const char* to_string(SubmitStatus s) noexcept {
    switch (s) {
        case SubmitStatus::Rejected:        return "Rejected";
        case SubmitStatus::Filled:          return "Filled";
        case SubmitStatus::Rested:          return "Rested";
        case SubmitStatus::PartiallyFilled: return "PartiallyFilled";
        case SubmitStatus::Cancelled:       return "Cancelled";
    }
    return "?";
}

struct SubmitResult {
    SubmitStatus status;
    RejectReason reason;
    Quantity     filled;   // quantity executed by this submission
    Quantity     resting;  // quantity now resting on the book (0 if none)

    static constexpr SubmitResult rejected(RejectReason r) noexcept {
        return SubmitResult{SubmitStatus::Rejected, r, 0, 0};
    }
};

}  // namespace lob
