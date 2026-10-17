#pragma once
// Implementation B: binary heap of prices with lazy deletion.
//
//   best price   : a binary heap (std::push_heap / std::pop_heap over a vector)
//                  holding every price that currently has at least one order.
//                  Max-heap for bids, min-heap for asks.
//   price levels : std::unordered_map<Tick, LevelInfo>, i.e. hashed, not sorted
//   orders       : stored inside their price level in a small vector, plus an
//                  id -> tick index (std::unordered_map) so a cancel knows which
//                  level to look in.
//
// "Lazy deletion": when a level becomes empty we do not search the heap for it
// (that would be O(n)). It stays in the heap and is thrown away only when it
// reaches the top. To keep get_bbo() trivially correct, cancel_order() pops
// empty levels off the top straight away, so the invariant is: the top of the
// heap is always a real, non-empty level.

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

#include "order_book.hpp"

class HeapBook {
public:
    static const char* name() { return "price heap (lazy delete) + hashed levels, orders inside levels"; }

    void add_order(uint64_t id, Side side, double price, long qty) {
        const Tick tick = to_tick(price);
        if (qty <= 0 || !tick_in_band(side, tick)) return;

        auto ins = index_.emplace(id, tick);
        if (!ins.second) return;  // duplicate id

        if (side == Side::Buy) add_impl(bids_, tick, id, qty);
        else                   add_impl(asks_, tick, id, qty);
    }

    bool cancel_order(uint64_t id) {
        auto it = index_.find(id);
        if (it == index_.end()) return false;

        const Tick tick = it->second;
        index_.erase(it);

        if (is_bid_tick(tick)) remove_impl(bids_, tick, id);
        else                   remove_impl(asks_, tick, id);
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (!bids_.heap.empty()) {
            const Tick t = bids_.heap.front();
            b.has_bid = true;
            b.bid_price = to_price(t);
            b.bid_qty = bids_.levels.find(t)->second.qty;
        }
        if (!asks_.heap.empty()) {
            const Tick t = asks_.heap.front();
            b.has_ask = true;
            b.ask_price = to_price(t);
            b.ask_qty = asks_.levels.find(t)->second.qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks_, depth); }

    size_t order_count() const { return index_.size(); }

private:
    struct OrderEntry {
        uint64_t id;
        long qty;
    };

    struct LevelInfo {
        long qty = 0;
        bool in_heap = false;
        std::vector<OrderEntry> orders;
    };

    // One of these per side. Compare = std::less for bids (max-heap) and
    // std::greater for asks (min-heap).
    template <class Compare>
    struct SideBook {
        std::unordered_map<Tick, LevelInfo> levels;
        std::vector<Tick> heap;
        Compare cmp;

        void push(Tick t) {
            heap.push_back(t);
            std::push_heap(heap.begin(), heap.end(), cmp);
        }
        void pop() {
            std::pop_heap(heap.begin(), heap.end(), cmp);
            heap.pop_back();
        }
        // Drop empty levels from the top until a real one (or nothing) is there.
        void pop_empty_top() {
            while (!heap.empty()) {
                auto it = levels.find(heap.front());
                if (it->second.qty > 0) break;
                levels.erase(it);
                pop();
            }
        }
    };

    template <class SB>
    static void add_impl(SB& sb, Tick tick, uint64_t id, long qty) {
        LevelInfo& lv = sb.levels[tick];
        lv.qty += qty;
        lv.orders.push_back(OrderEntry{id, qty});
        if (!lv.in_heap) {
            lv.in_heap = true;
            sb.push(tick);
        }
    }

    template <class SB>
    static void remove_impl(SB& sb, Tick tick, uint64_t id) {
        LevelInfo& lv = sb.levels.find(tick)->second;
        std::vector<OrderEntry>& v = lv.orders;
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i].id == id) {
                lv.qty -= v[i].qty;
                v[i] = v.back();  // order inside a level does not matter for this book
                v.pop_back();
                break;
            }
        }
        sb.pop_empty_top();
    }

    // A heap only gives cheap access to the top element, so to list the best
    // `depth` levels we copy the heap and pop from the copy. Empty (lazily
    // deleted) levels are skipped while popping.
    template <class SB>
    static std::vector<Level> top_levels(const SB& sb, int depth) {
        std::vector<Level> out;
        if (depth <= 0 || sb.heap.empty()) return out;

        std::vector<Tick> h = sb.heap;
        while (!h.empty() && static_cast<int>(out.size()) < depth) {
            std::pop_heap(h.begin(), h.end(), sb.cmp);
            const Tick t = h.back();
            h.pop_back();
            const long qty = sb.levels.find(t)->second.qty;
            if (qty > 0) out.push_back(Level{to_price(t), qty});
        }
        return out;
    }

    SideBook<std::less<Tick>> bids_;
    SideBook<std::greater<Tick>> asks_;
    std::unordered_map<uint64_t, Tick> index_;
};
