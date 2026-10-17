#pragma once
// Common types shared by the three order book implementations.
//
// The public API is the one from the assignment (double prices). Internally
// every implementation converts the price to an integer tick (price * 100)
// because the tick size is fixed at 0.01 and integers are cheaper to compare,
// hash and use as array indices than doubles.

#include <cmath>
#include <cstdint>
#include <vector>

enum class Side { Buy, Sell };

struct Level {
    double price;
    long qty;
};

struct BBO {
    bool has_bid;
    double bid_price;
    long bid_qty;

    bool has_ask;
    double ask_price;
    long ask_qty;
};

using Tick = int32_t;

// Price bands from the assignment. Bids live in [50.00, 99.99] and asks in
// [100.01, 150.00], which gives 5000 possible levels on each side.
constexpr Tick kMinBidTick = 5000;   // 50.00
constexpr Tick kMaxBidTick = 9999;   // 99.99
constexpr Tick kMinAskTick = 10001;  // 100.01
constexpr Tick kMaxAskTick = 15000;  // 150.00
constexpr int kLevelsPerSide = 5000;

inline Tick to_tick(double price) {
    return static_cast<Tick>(std::lround(price * 100.0));
}

inline double to_price(Tick tick) {
    return static_cast<double>(tick) / 100.0;
}

// A tick on its own tells us the side: everything below 100.00 is a bid.
inline bool is_bid_tick(Tick tick) {
    return tick <= kMaxBidTick;
}

inline bool tick_in_band(Side side, Tick tick) {
    if (side == Side::Buy) return tick >= kMinBidTick && tick <= kMaxBidTick;
    return tick >= kMinAskTick && tick <= kMaxAskTick;
}
