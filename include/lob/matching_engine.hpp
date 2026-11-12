#pragma once

// Price-time priority matching engine.
//
// Incoming orders are matched against the opposite side of an OrderBook from
// the best price inwards, oldest order first at each level. Executions happen
// at the resting order's price. Supported:
//   * Limit  GTC  - remainder rests
//   * Limit  IOC  - remainder cancelled
//   * Limit  FOK  - all or nothing (liquidity is checked before any fill)
//   * Market      - sweeps the book at any price, remainder cancelled
//   * cancel      - O(1) via the order index
//   * modify      - quantity reduction keeps priority; any other change is a
//                   cancel/replace that re-enters matching (may trade)
//
// Trades are delivered through a callback so the hot path performs no
// allocation; a std::vector overload is provided for convenience and tests.

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

#include "lob/order_book.hpp"
#include "lob/types.hpp"

namespace lob {

class MatchingEngine {
public:
    explicit MatchingEngine(OrderBook& book) : book_(book) {}

    OrderBook&       book() noexcept       { return book_; }
    const OrderBook& book() const noexcept { return book_; }

    template <typename OnTrade>
    SubmitResult submit(const Order& order, OnTrade&& on_trade) {
        if (order.quantity == 0) {
            return SubmitResult::rejected(RejectReason::ZeroQuantity);
        }
        if (book_.contains(order.id)) {
            return SubmitResult::rejected(RejectReason::DuplicateId);
        }
        if (order.type == OrderType::Limit && !book_.in_range(order.price)) {
            return SubmitResult::rejected(RejectReason::PriceOutOfRange);
        }
        if (order.tif == TimeInForce::FOK && available_liquidity(order) < order.quantity) {
            return SubmitResult::rejected(RejectReason::InsufficientLiquidity);
        }

        const Side resting_side = opposite(order.side);
        Quantity remaining = order.quantity;

        while (remaining > 0) {
            const PriceLevel* level = book_.best(resting_side);
            if (level == nullptr) break;
            if (order.type == OrderType::Limit && !crosses(order.side, order.price, level->price)) break;

            const OrderNode& maker = book_.front(*level);
            const Quantity fill = std::min(remaining, maker.quantity);

            Trade trade;
            trade.price     = level->price;
            trade.quantity  = fill;
            trade.aggressor = order.side;
            if (order.side == Side::Buy) {
                trade.buy_order_id  = order.id;
                trade.sell_order_id = maker.id;
            } else {
                trade.buy_order_id  = maker.id;
                trade.sell_order_id = order.id;
            }

            remaining -= fill;
            book_.fill_front(resting_side, fill);  // invalidates `maker` and `level`
            on_trade(trade);
        }

        const Quantity filled = order.quantity - remaining;

        if (remaining == 0) {
            return SubmitResult{SubmitStatus::Filled, RejectReason::None, filled, 0};
        }

        if (order.type == OrderType::Limit && order.tif == TimeInForce::GTC) {
            Order rest = order;
            rest.quantity = remaining;
            [[maybe_unused]] const RejectReason r = book_.add(rest);
            assert(r == RejectReason::None);
            return SubmitResult{filled > 0 ? SubmitStatus::PartiallyFilled : SubmitStatus::Rested,
                                RejectReason::None, filled, remaining};
        }

        // Market, IOC (and FOK, which cannot reach here with remaining > 0).
        return SubmitResult{filled > 0 ? SubmitStatus::PartiallyFilled : SubmitStatus::Cancelled,
                            RejectReason::None, filled, 0};
    }

    // Convenience overload: clears `trades` and appends every execution.
    SubmitResult submit(const Order& order, std::vector<Trade>& trades) {
        trades.clear();
        return submit(order, [&trades](const Trade& t) { trades.push_back(t); });
    }

    bool cancel(OrderId id) noexcept { return book_.cancel(id); }

    // Cancel/replace semantics:
    //   * same price and smaller quantity -> reduce in place, priority kept
    //   * anything else -> cancel and re-submit as a new GTC limit with the
    //     same id; it goes to the back of the queue and may execute.
    template <typename OnTrade>
    SubmitResult modify(OrderId id, Price new_price, Quantity new_quantity, OnTrade&& on_trade) {
        const OrderNode* node = book_.find(id);
        if (node == nullptr) {
            return SubmitResult::rejected(RejectReason::UnknownOrder);
        }
        if (new_quantity == 0) {
            return SubmitResult::rejected(RejectReason::ZeroQuantity);
        }
        if (!book_.in_range(new_price)) {
            return SubmitResult::rejected(RejectReason::PriceOutOfRange);
        }
        if (new_price == node->price && new_quantity < node->quantity) {
            book_.reduce(id, new_quantity);
            return SubmitResult{SubmitStatus::Rested, RejectReason::None, 0, new_quantity};
        }

        Order replacement{id, new_price, new_quantity, node->side, OrderType::Limit, TimeInForce::GTC};
        book_.cancel(id);
        return submit(replacement, std::forward<OnTrade>(on_trade));
    }

    SubmitResult modify(OrderId id, Price new_price, Quantity new_quantity, std::vector<Trade>& trades) {
        trades.clear();
        return modify(id, new_price, new_quantity, [&trades](const Trade& t) { trades.push_back(t); });
    }

private:
    static bool crosses(Side incoming, Price limit, Price resting) noexcept {
        return incoming == Side::Buy ? limit >= resting : limit <= resting;
    }

    // Total resting quantity the order could execute against (for FOK).
    std::uint64_t available_liquidity(const Order& order) const noexcept {
        std::uint64_t total = 0;
        book_.for_each_level(opposite(order.side), [&](const PriceLevel& lvl) {
            if (order.type == OrderType::Limit && !crosses(order.side, order.price, lvl.price)) return false;
            total += lvl.total_quantity;
            return total < order.quantity;  // stop as soon as we have enough
        });
        return total;
    }

    OrderBook& book_;
};

}  // namespace lob
