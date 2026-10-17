#pragma once
// Implementation C: my own design. Everything is a flat array and nothing is
// allocated per order or per level.
//
//   price levels : one int32 quantity per possible tick, in a plain array
//                  (5000 entries per side). The array index is derived from
//                  the tick so there is no search at all.
//   best price   : a two-level bitmap over the level array. Bit i of `occ` is
//                  set when level i has quantity; bit w of `summary` is set
//                  when word w of `occ` is non-zero. Finding the highest set
//                  bit is then at most 2 summary words + 1 occ word, using
//                  the clz instruction. The current best is also cached so
//                  get_bbo() is just two loads.
//   orders       : a hand written open-addressing hash table (linear probing,
//                  Fibonacci hashing, backward-shift deletion). Each slot is
//                  16 bytes: {id, qty, tick}. The table is sized up front so
//                  the hot path never calls malloc.
//
// Both sides are indexed so that a larger index means a better price:
//   bids : index = tick - 5000   (50.00 -> 0, 99.99 -> 4999)
//   asks : index = 15000 - tick  (150.00 -> 0, 100.01 -> 4999)
// which lets the two sides share exactly the same code.

#include <cstring>
#include <vector>

#include "order_book.hpp"

class ArrayBook {
public:
    static const char* name() { return "flat arrays + two-level bitmap + open-addressing hash"; }

    // expected_orders is the number of live orders we want to hold without
    // rehashing. The table grows on its own if we go past it.
    explicit ArrayBook(size_t expected_orders = 1u << 19) : orders_(expected_orders) {}

    void add_order(uint64_t id, Side side, double price, long qty) {
        const Tick tick = to_tick(price);
        if (qty <= 0 || !tick_in_band(side, tick)) return;
        if (!orders_.insert(id, tick, static_cast<int32_t>(qty))) return;  // duplicate id

        if (side == Side::Buy) bids_.add(tick - kMinBidTick, static_cast<int32_t>(qty));
        else                   asks_.add(kMaxAskTick - tick, static_cast<int32_t>(qty));
    }

    bool cancel_order(uint64_t id) {
        int32_t tick, qty;
        if (!orders_.erase(id, tick, qty)) return false;

        if (is_bid_tick(tick)) bids_.remove(tick - kMinBidTick, qty);
        else                   asks_.remove(kMaxAskTick - tick, qty);
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (bids_.best >= 0) {
            b.has_bid = true;
            b.bid_price = to_price(bid_tick(bids_.best));
            b.bid_qty = bids_.qty[bids_.best];
        }
        if (asks_.best >= 0) {
            b.has_ask = true;
            b.ask_price = to_price(ask_tick(asks_.best));
            b.ask_qty = asks_.qty[asks_.best];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int i = bids_.best; i >= 0 && static_cast<int>(out.size()) < depth; i = bids_.highest_below(i))
            out.push_back(Level{to_price(bid_tick(i)), bids_.qty[i]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int i = asks_.best; i >= 0 && static_cast<int>(out.size()) < depth; i = asks_.highest_below(i))
            out.push_back(Level{to_price(ask_tick(i)), asks_.qty[i]});
        return out;
    }

    size_t order_count() const { return orders_.size(); }
    size_t order_table_capacity() const { return orders_.capacity(); }

private:
    static Tick bid_tick(int index) { return kMinBidTick + index; }
    static Tick ask_tick(int index) { return kMaxAskTick - index; }

    static int clz64(uint64_t x) { return __builtin_clzll(x); }

    // ---- price levels for one side ------------------------------------
    struct SideLevels {
        static constexpr int kWords = (kLevelsPerSide + 63) / 64;    // 79
        static constexpr int kSummaryWords = (kWords + 63) / 64;     // 2

        int32_t qty[kLevelsPerSide];
        uint64_t occ[kWords];
        uint64_t summary[kSummaryWords];
        int best;  // highest occupied index, -1 when the side is empty

        SideLevels() : best(-1) {
            std::memset(qty, 0, sizeof(qty));
            std::memset(occ, 0, sizeof(occ));
            std::memset(summary, 0, sizeof(summary));
        }

        void add(int i, int32_t q) {
            if (qty[i] == 0) {
                const int w = i >> 6;
                occ[w] |= 1ull << (i & 63);
                summary[w >> 6] |= 1ull << (w & 63);
            }
            qty[i] += q;
            if (i > best) best = i;
        }

        void remove(int i, int32_t q) {
            qty[i] -= q;
            if (qty[i] == 0) {
                const int w = i >> 6;
                occ[w] &= ~(1ull << (i & 63));
                if (occ[w] == 0) summary[w >> 6] &= ~(1ull << (w & 63));
                if (i == best) best = highest_below(i);
            }
        }

        // Highest occupied index strictly below i, or -1.
        int highest_below(int i) const {
            int w = i >> 6;
            const uint64_t below = occ[w] & ((1ull << (i & 63)) - 1);
            if (below) return (w << 6) + 63 - clz64(below);

            // Nothing left in this word: ask the summary which word below w
            // still has something in it.
            int sw = w >> 6;
            uint64_t sbits = summary[sw] & ((1ull << (w & 63)) - 1);
            for (;;) {
                if (sbits) {
                    w = (sw << 6) + 63 - clz64(sbits);
                    return (w << 6) + 63 - clz64(occ[w]);
                }
                if (--sw < 0) return -1;
                sbits = summary[sw];
            }
        }
    };

    // ---- order table ----------------------------------------------------
    class OrderTable {
    public:
        struct Slot {
            uint64_t id;
            int32_t qty;
            int32_t tick;
        };
        static constexpr uint64_t kEmpty = ~0ull;  // this id value is reserved

        explicit OrderTable(size_t expected) {
            size_t cap = 64;
            while (cap < expected * 2) cap <<= 1;
            init(cap);
        }

        bool insert(uint64_t id, int32_t tick, int32_t qty) {
            if (id == kEmpty) return false;
            if ((size_ + 1) * 4 > slots_.size() * 3) grow();  // keep load below 75%

            size_t i = home(id);
            while (slots_[i].id != kEmpty) {
                if (slots_[i].id == id) return false;
                i = (i + 1) & mask_;
            }
            slots_[i] = Slot{id, qty, tick};
            ++size_;
            return true;
        }

        // Removes the order and reports its tick and quantity. Uses backward
        // shift deletion instead of tombstones, so lookups never degrade.
        bool erase(uint64_t id, int32_t& tick, int32_t& qty) {
            size_t i = home(id);
            while (slots_[i].id != id) {
                if (slots_[i].id == kEmpty) return false;
                i = (i + 1) & mask_;
            }
            tick = slots_[i].tick;
            qty = slots_[i].qty;

            size_t hole = i;
            for (size_t j = (hole + 1) & mask_; slots_[j].id != kEmpty; j = (j + 1) & mask_) {
                const size_t h = home(slots_[j].id);
                // The entry at j may move into the hole only if the hole lies
                // on its probe path, i.e. between its home slot and j.
                const bool movable = (j > hole) ? (h <= hole || h > j) : (h <= hole && h > j);
                if (movable) {
                    slots_[hole] = slots_[j];
                    hole = j;
                }
            }
            slots_[hole].id = kEmpty;
            --size_;
            return true;
        }

        size_t size() const { return size_; }
        size_t capacity() const { return slots_.size(); }

    private:
        void init(size_t cap) {
            slots_.assign(cap, Slot{kEmpty, 0, 0});
            mask_ = cap - 1;
            shift_ = 64 - __builtin_ctzll(cap);
        }

        size_t home(uint64_t id) const {
            return static_cast<size_t>((id * 0x9E3779B97F4A7C15ull) >> shift_);
        }

        void grow() {
            std::vector<Slot> old;
            old.swap(slots_);
            init(old.size() * 2);
            size_ = 0;
            for (const Slot& s : old)
                if (s.id != kEmpty) insert(s.id, s.tick, s.qty);
        }

        std::vector<Slot> slots_;
        size_t mask_ = 0;
        unsigned shift_ = 64;
        size_t size_ = 0;
    };

    SideLevels bids_;
    SideLevels asks_;
    OrderTable orders_;
};
