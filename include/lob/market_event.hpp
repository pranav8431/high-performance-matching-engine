#pragma once

// Normalised market data / order entry event, the unit the replay tool and
// benchmarks feed to the engine. Fixed size and trivially copyable so a
// workload can be pre-generated into a flat std::vector.

#include <cstdint>

#include "lob/types.hpp"

namespace lob {

enum class EventType : std::uint8_t {
    Add,     // new GTC limit order
    Market,  // market order
    Ioc,     // immediate-or-cancel limit order
    Fok,     // fill-or-kill limit order
    Cancel,  // cancel a resting order
    Modify   // cancel/replace a resting order (new price and quantity)
};

constexpr const char* to_string(EventType t) noexcept {
    switch (t) {
        case EventType::Add:    return "ADD";
        case EventType::Market: return "MARKET";
        case EventType::Ioc:    return "IOC";
        case EventType::Fok:    return "FOK";
        case EventType::Cancel: return "CANCEL";
        case EventType::Modify: return "MODIFY";
    }
    return "?";
}

struct MarketEvent {
    EventType type;
    Side      side;      // unused for Cancel / Modify
    OrderId   order_id;
    Price     price;     // unused for Cancel / Market
    Quantity  quantity;  // unused for Cancel
};

}  // namespace lob
