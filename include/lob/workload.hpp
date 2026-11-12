#pragma once

// Synthetic order-flow generator used by benchmarks, the replay tool and the
// differential test. It models a single instrument whose mid price performs a
// slow random walk; passive orders cluster near the touch (geometric offset),
// a configurable share of orders cross the spread, and cancels / modifies
// target orders the generator believes are still live.
//
// Generation is deterministic for a given seed so results are reproducible.

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include "lob/market_event.hpp"

namespace lob {

struct WorkloadConfig {
    std::uint64_t seed       = 42;
    Price         min_price  = 1;
    Price         max_price  = 200'000;
    Price         start_mid  = 100'000;
    Price         half_spread = 2;      // ticks from mid to each touch
    int           max_offset = 50;      // deepest passive level in ticks
    double        offset_p   = 0.25;    // geometric decay of passive depth
    Quantity      max_quantity = 500;             // passive order size
    Quantity      max_aggressive_quantity = 100;  // taker size; keeps cancel:trade realistic

    // Event mix in percent; must sum to 100.
    int add_pct        = 58;  // passive GTC limit
    int aggressive_pct = 6;   // crossing GTC limit (hits the book, may rest)
    int ioc_pct        = 2;
    int market_pct     = 1;
    int cancel_pct     = 26;
    int modify_pct     = 7;

    // Probability per event that the mid moves one tick.
    double drift_prob = 0.0005;

    // Cancels / modifies target one of the most recent `recent_window` live
    // orders with probability `recent_prob`, otherwise any live order. Real
    // order flow is dominated by short-lived quotes, so most cancels hit
    // orders that are still resting.
    std::size_t recent_window = 2'000;
    double      recent_prob   = 0.9;
};

class WorkloadGenerator {
public:
    explicit WorkloadGenerator(WorkloadConfig cfg = {})
        : cfg_(cfg), rng_(cfg.seed), mid_(cfg.start_mid),
          mix_(0, 99), qty_(1, cfg.max_quantity), taker_qty_(1, cfg.max_aggressive_quantity),
          coin_(0.0, 1.0),
          offset_(cfg.offset_p) {
        live_.reserve(1 << 16);
    }

    MarketEvent next() {
        maybe_drift();

        int r = mix_(rng_);
        MarketEvent e{};

        if ((r -= cfg_.add_pct) < 0) {
            e = passive_add(EventType::Add);
        } else if ((r -= cfg_.aggressive_pct) < 0) {
            e = aggressive(EventType::Add);
        } else if ((r -= cfg_.ioc_pct) < 0) {
            e = aggressive(EventType::Ioc);
        } else if ((r -= cfg_.market_pct) < 0) {
            e.type = EventType::Market;
            e.side = coin_(rng_) < 0.5 ? Side::Buy : Side::Sell;
            e.order_id = next_id_++;
            e.quantity = taker_qty_(rng_);
        } else if ((r -= cfg_.cancel_pct) < 0) {
            if (live_.empty()) return passive_add(EventType::Add);
            e.type = EventType::Cancel;
            e.order_id = take_live();
        } else {
            if (live_.empty()) return passive_add(EventType::Add);
            e.type = EventType::Modify;
            e.order_id = live_[pick_live_index()];
            // Re-price near the touch on a random side; quantity random.
            const Side s = coin_(rng_) < 0.5 ? Side::Buy : Side::Sell;
            e.price = passive_price(s);
            e.quantity = qty_(rng_);
        }
        return e;
    }

    std::vector<MarketEvent> generate(std::size_t count) {
        std::vector<MarketEvent> out;
        out.reserve(count);
        for (std::size_t i = 0; i < count; ++i) out.push_back(next());
        return out;
    }

    const WorkloadConfig& config() const noexcept { return cfg_; }
    Price mid() const noexcept { return mid_; }

private:
    void maybe_drift() {
        if (coin_(rng_) < cfg_.drift_prob) {
            mid_ += coin_(rng_) < 0.5 ? -1 : 1;
            const Price lo = cfg_.min_price + cfg_.max_offset + cfg_.half_spread + 1;
            const Price hi = cfg_.max_price - cfg_.max_offset - cfg_.half_spread - 1;
            mid_ = std::clamp(mid_, lo, hi);
        }
    }

    int offset() {
        const int o = offset_(rng_);
        return o > cfg_.max_offset ? cfg_.max_offset : o;
    }

    Price passive_price(Side s) {
        const Price o = cfg_.half_spread + offset();
        return s == Side::Buy ? mid_ - o : mid_ + o;
    }

    MarketEvent passive_add(EventType type) {
        MarketEvent e{};
        e.type = type;
        e.side = coin_(rng_) < 0.5 ? Side::Buy : Side::Sell;
        e.order_id = next_id_++;
        e.price = passive_price(e.side);
        e.quantity = qty_(rng_);
        live_.push_back(e.order_id);
        return e;
    }

    // Crosses the spread by a small random amount.
    MarketEvent aggressive(EventType type) {
        MarketEvent e{};
        e.type = type;
        e.side = coin_(rng_) < 0.5 ? Side::Buy : Side::Sell;
        e.order_id = next_id_++;
        const Price through = offset() / 2;
        e.price = e.side == Side::Buy ? mid_ + cfg_.half_spread + through
                                      : mid_ - cfg_.half_spread - through;
        e.quantity = taker_qty_(rng_);
        if (type == EventType::Add) live_.push_back(e.order_id);  // may rest
        return e;
    }

    std::size_t pick_live_index() {
        std::size_t lo = 0;
        if (coin_(rng_) < cfg_.recent_prob && live_.size() > cfg_.recent_window) {
            lo = live_.size() - cfg_.recent_window;
        }
        return std::uniform_int_distribution<std::size_t>(lo, live_.size() - 1)(rng_);
    }

    OrderId take_live() {
        const std::size_t i = pick_live_index();
        const OrderId id = live_[i];
        live_[i] = live_.back();
        live_.pop_back();
        return id;
    }

    WorkloadConfig       cfg_;
    std::mt19937_64      rng_;
    Price                mid_;
    OrderId              next_id_ = 1;
    std::vector<OrderId> live_;

    std::uniform_int_distribution<int>      mix_;
    std::uniform_int_distribution<Quantity> qty_;
    std::uniform_int_distribution<Quantity> taker_qty_;
    std::uniform_real_distribution<double>  coin_;
    std::geometric_distribution<int>        offset_;
};

}  // namespace lob
