#pragma once
// Implementation A: standard library containers.
//
//   price levels : std::map<Tick, LevelInfo>   (red-black tree, one per side)
//   orders       : std::unordered_map<uint64_t, Order>
//
// Best bid/ask is simply begin() of the respective map (bids are sorted with
// std::greater so the highest price comes first). Cancelling an order is a
// hash lookup to get its price, then a tree lookup to find the level.

#include <functional>
#include <map>
#include <unordered_map>

#include "order_book.hpp"

class MapBook {
public:
    static const char* name() { return "std::map levels + std::unordered_map orders"; }

    void add_order(uint64_t id, Side side, double price, long qty) {
        const Tick tick = to_tick(price);
        if (qty <= 0 || !tick_in_band(side, tick)) return;

        auto ins = orders_.emplace(id, Order{side, tick, qty});
        if (!ins.second) return;  // duplicate id, ignore it

        if (side == Side::Buy) add_to_level(bids_, tick, qty);
        else                   add_to_level(asks_, tick, qty);
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        const Order o = it->second;
        orders_.erase(it);

        if (o.side == Side::Buy) remove_from_level(bids_, o.tick, o.qty);
        else                     remove_from_level(asks_, o.tick, o.qty);
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (!bids_.empty()) {
            auto it = bids_.begin();
            b.has_bid = true;
            b.bid_price = to_price(it->first);
            b.bid_qty = it->second.qty;
        }
        if (!asks_.empty()) {
            auto it = asks_.begin();
            b.has_ask = true;
            b.ask_price = to_price(it->first);
            b.ask_qty = it->second.qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks_, depth); }

    size_t order_count() const { return orders_.size(); }

private:
    struct Order {
        Side side;
        Tick tick;
        long qty;
    };

    struct LevelInfo {
        long qty = 0;    // sum of all order quantities at this price
        int orders = 0;  // how many orders rest here, so we know when to erase the level
    };

    using BidMap = std::map<Tick, LevelInfo, std::greater<Tick>>;
    using AskMap = std::map<Tick, LevelInfo>;

    template <class M>
    static void add_to_level(M& m, Tick tick, long qty) {
        LevelInfo& lv = m[tick];  // inserts the level if it is new
        lv.qty += qty;
        lv.orders += 1;
    }

    template <class M>
    static void remove_from_level(M& m, Tick tick, long qty) {
        auto it = m.find(tick);
        it->second.qty -= qty;
        if (--it->second.orders == 0) m.erase(it);
    }

    template <class M>
    static std::vector<Level> top_levels(const M& m, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        for (auto it = m.begin(); it != m.end() && static_cast<int>(out.size()) < depth; ++it)
            out.push_back(Level{to_price(it->first), it->second.qty});
        return out;
    }

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<uint64_t, Order> orders_;
};
