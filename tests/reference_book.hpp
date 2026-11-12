#pragma once

// Deliberately naive reference implementation of the same matching semantics,
// built from std::map / std::deque / std::unordered_map. It is slow and
// obviously correct, and the differential test checks the optimised engine
// against it operation by operation.

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "lob/types.hpp"

namespace lob::test {

class ReferenceBook {
public:
    ReferenceBook(Price min_price, Price max_price) : min_(min_price), max_(max_price) {}

    SubmitResult submit(const Order& order, std::vector<Trade>& trades) {
        trades.clear();
        if (order.quantity == 0) return SubmitResult::rejected(RejectReason::ZeroQuantity);
        if (where_.count(order.id)) return SubmitResult::rejected(RejectReason::DuplicateId);
        if (order.type == OrderType::Limit && (order.price < min_ || order.price > max_))
            return SubmitResult::rejected(RejectReason::PriceOutOfRange);
        if (order.tif == TimeInForce::FOK && liquidity(order) < order.quantity)
            return SubmitResult::rejected(RejectReason::InsufficientLiquidity);

        Quantity remaining = order.quantity;
        if (order.side == Side::Buy) match(order, remaining, asks_, trades);
        else                         match(order, remaining, bids_, trades);

        const Quantity filled = order.quantity - remaining;
        if (remaining == 0) return {SubmitStatus::Filled, RejectReason::None, filled, 0};
        if (order.type == OrderType::Limit && order.tif == TimeInForce::GTC) {
            Order rest = order;
            rest.quantity = remaining;
            rest_order(rest);
            return {filled ? SubmitStatus::PartiallyFilled : SubmitStatus::Rested, RejectReason::None, filled, remaining};
        }
        return {filled ? SubmitStatus::PartiallyFilled : SubmitStatus::Cancelled, RejectReason::None, filled, 0};
    }

    bool cancel(OrderId id) {
        auto it = where_.find(id);
        if (it == where_.end()) return false;
        const auto [side, price] = it->second;
        if (side == Side::Buy) erase_from(bids_, price, id); else erase_from(asks_, price, id);
        where_.erase(it);
        return true;
    }

    SubmitResult modify(OrderId id, Price new_price, Quantity new_qty, std::vector<Trade>& trades) {
        trades.clear();
        auto it = where_.find(id);
        if (it == where_.end()) return SubmitResult::rejected(RejectReason::UnknownOrder);
        if (new_qty == 0) return SubmitResult::rejected(RejectReason::ZeroQuantity);
        if (new_price < min_ || new_price > max_) return SubmitResult::rejected(RejectReason::PriceOutOfRange);
        const auto [side, price] = it->second;
        Order* existing = find_order(side, price, id);
        if (new_price == price && new_qty < existing->quantity) {
            existing->quantity = new_qty;
            return {SubmitStatus::Rested, RejectReason::None, 0, new_qty};
        }
        Order replacement{id, new_price, new_qty, side, OrderType::Limit, TimeInForce::GTC};
        cancel(id);
        return submit(replacement, trades);
    }

    // Snapshot of one side as [(price, [(id, qty)...])...] in priority order.
    using LevelSnapshot = std::vector<std::pair<Price, std::vector<std::pair<OrderId, Quantity>>>>;

    LevelSnapshot snapshot(Side s) const {
        LevelSnapshot out;
        auto dump = [&out](const auto& levels) {
            for (const auto& [price, q] : levels) {
                std::vector<std::pair<OrderId, Quantity>> orders;
                for (const Order& o : q) orders.emplace_back(o.id, o.quantity);
                out.emplace_back(price, std::move(orders));
            }
        };
        if (s == Side::Buy) dump(bids_); else dump(asks_);
        return out;
    }

    std::size_t order_count() const { return where_.size(); }

private:
    using Bids = std::map<Price, std::deque<Order>, std::greater<Price>>;
    using Asks = std::map<Price, std::deque<Order>>;

    static bool crosses(Side incoming, Price limit, Price resting) {
        return incoming == Side::Buy ? limit >= resting : limit <= resting;
    }

    template <typename Levels>
    void match(const Order& order, Quantity& remaining, Levels& levels, std::vector<Trade>& trades) {
        while (remaining > 0 && !levels.empty()) {
            auto lvl = levels.begin();
            if (order.type == OrderType::Limit && !crosses(order.side, order.price, lvl->first)) break;
            Order& maker = lvl->second.front();
            const Quantity fill = std::min(remaining, maker.quantity);
            Trade t{};
            t.price = lvl->first;
            t.quantity = fill;
            t.aggressor = order.side;
            if (order.side == Side::Buy) { t.buy_order_id = order.id; t.sell_order_id = maker.id; }
            else                         { t.buy_order_id = maker.id; t.sell_order_id = order.id; }
            trades.push_back(t);
            remaining -= fill;
            maker.quantity -= fill;
            if (maker.quantity == 0) {
                where_.erase(maker.id);
                lvl->second.pop_front();
                if (lvl->second.empty()) levels.erase(lvl);
            }
        }
    }

    std::uint64_t liquidity(const Order& order) const {
        std::uint64_t total = 0;
        auto sum = [&](const auto& levels) {
            for (const auto& [price, q] : levels) {
                if (order.type == OrderType::Limit && !crosses(order.side, order.price, price)) break;
                for (const Order& o : q) total += o.quantity;
            }
        };
        if (order.side == Side::Buy) sum(asks_); else sum(bids_);
        return total;
    }

    void rest_order(const Order& o) {
        if (o.side == Side::Buy) bids_[o.price].push_back(o); else asks_[o.price].push_back(o);
        where_[o.id] = {o.side, o.price};
    }

    template <typename Levels>
    static void erase_from(Levels& levels, Price price, OrderId id) {
        auto lvl = levels.find(price);
        auto& q = lvl->second;
        q.erase(std::find_if(q.begin(), q.end(), [id](const Order& o) { return o.id == id; }));
        if (q.empty()) levels.erase(lvl);
    }

    Order* find_order(Side side, Price price, OrderId id) {
        auto& q = side == Side::Buy ? bids_[price] : asks_[price];
        for (Order& o : q) if (o.id == id) return &o;
        return nullptr;
    }

    Price min_, max_;
    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, std::pair<Side, Price>> where_;
};

}  // namespace lob::test
