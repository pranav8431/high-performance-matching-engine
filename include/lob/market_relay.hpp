#pragma once

// Routes MarketEvents to the MatchingEngine and keeps running statistics.
// This is the glue between a feed (file replay, generator, network) and the
// engine; it owns no order state itself.

#include <cstdint>

#include "lob/market_event.hpp"
#include "lob/matching_engine.hpp"

namespace lob {

struct RelayStats {
    std::uint64_t events          = 0;
    std::uint64_t adds            = 0;
    std::uint64_t markets         = 0;
    std::uint64_t iocs            = 0;
    std::uint64_t foks            = 0;
    std::uint64_t cancels         = 0;
    std::uint64_t cancel_misses   = 0;  // cancel of an id no longer resting
    std::uint64_t modifies        = 0;
    std::uint64_t modify_misses   = 0;
    std::uint64_t rejects         = 0;  // rejected submissions (bad price, dup id, FOK kill, ...)
    std::uint64_t trades          = 0;
    std::uint64_t traded_quantity = 0;
};

// Sink that discards trades; use when only throughput / final state matter.
struct NullTradeSink {
    void operator()(const Trade&) const noexcept {}
};

template <typename TradeSink = NullTradeSink>
class MarketRelay {
public:
    explicit MarketRelay(MatchingEngine& engine, TradeSink sink = TradeSink{})
        : engine_(engine), sink_(sink) {}

    void process(const MarketEvent& e) {
        ++stats_.events;
        switch (e.type) {
            case EventType::Add:
                ++stats_.adds;
                submit(Order{e.order_id, e.price, e.quantity, e.side, OrderType::Limit, TimeInForce::GTC});
                break;
            case EventType::Market:
                ++stats_.markets;
                submit(Order{e.order_id, 0, e.quantity, e.side, OrderType::Market, TimeInForce::IOC});
                break;
            case EventType::Ioc:
                ++stats_.iocs;
                submit(Order{e.order_id, e.price, e.quantity, e.side, OrderType::Limit, TimeInForce::IOC});
                break;
            case EventType::Fok:
                ++stats_.foks;
                submit(Order{e.order_id, e.price, e.quantity, e.side, OrderType::Limit, TimeInForce::FOK});
                break;
            case EventType::Cancel:
                ++stats_.cancels;
                if (!engine_.cancel(e.order_id)) ++stats_.cancel_misses;
                break;
            case EventType::Modify: {
                ++stats_.modifies;
                const SubmitResult r = engine_.modify(e.order_id, e.price, e.quantity, trade_counter());
                if (r.status == SubmitStatus::Rejected) {
                    if (r.reason == RejectReason::UnknownOrder) ++stats_.modify_misses; else ++stats_.rejects;
                }
                break;
            }
        }
    }

    const RelayStats& stats() const noexcept { return stats_; }
    MatchingEngine&   engine() noexcept { return engine_; }

private:
    auto trade_counter() {
        return [this](const Trade& t) {
            ++stats_.trades;
            stats_.traded_quantity += t.quantity;
            sink_(t);
        };
    }

    void submit(const Order& order) {
        const SubmitResult r = engine_.submit(order, trade_counter());
        if (r.status == SubmitStatus::Rejected) ++stats_.rejects;
    }

    MatchingEngine& engine_;
    TradeSink       sink_;
    RelayStats      stats_;
};

}  // namespace lob
