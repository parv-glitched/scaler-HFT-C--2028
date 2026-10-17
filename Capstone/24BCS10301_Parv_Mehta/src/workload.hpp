#pragma once
// Deterministic random workload shared by the benchmark and the verify mode.
//
// The whole operation list is generated up front, before any timing starts,
// so that (a) all three implementations see byte-for-byte the same sequence
// and (b) the cost of the random number generator and of picking a live
// order to cancel is not mixed into the measurements.

#include <random>
#include <vector>

#include "order_book.hpp"

enum class OpType : uint8_t { Add, Cancel, Bbo };

struct Op {
    OpType type;
    Side side;      // Add only
    uint64_t id;    // Add / Cancel
    Tick tick;      // Add only
    int32_t qty;    // Add only
};

struct WorkloadConfig {
    uint32_t seed = 42;
    size_t ops = 1000000;
    int add_pct = 70;
    int cancel_pct = 20;   // the rest are BBO queries
    int band = kLevelsPerSide;  // how many price levels per side to actually use
};

struct WorkloadStats {
    size_t adds = 0, cancels = 0, bbos = 0;
    size_t live_at_end = 0;
};

inline std::vector<Op> generate_workload(const WorkloadConfig& cfg, WorkloadStats* stats = nullptr) {
    std::mt19937 rng(cfg.seed);
    // Not using std::uniform_int_distribution on purpose: its output differs
    // between libstdc++ and libc++, and I want the same workload on the Mac
    // and inside the Linux container. The modulo bias is irrelevant here.
    auto uniform = [&rng](uint32_t n) -> uint32_t { return rng() % n; };

    std::vector<Op> ops;
    ops.reserve(cfg.ops);

    // Live order ids, plus where each id sits in `live` so a random one can
    // be removed in O(1). Ids are handed out sequentially from 1, so a plain
    // vector indexed by id does the job (and keeps malloc out of the way when
    // this runs under perf).
    std::vector<uint64_t> live;
    std::vector<uint32_t> pos(cfg.ops + 1, 0);
    uint64_t next_id = 1;
    WorkloadStats st;

    for (size_t i = 0; i < cfg.ops; ++i) {
        uint32_t r = uniform(100);
        Op op{};

        const bool want_cancel = r >= static_cast<uint32_t>(cfg.add_pct) &&
                                 r < static_cast<uint32_t>(cfg.add_pct + cfg.cancel_pct);
        if (want_cancel && !live.empty()) {
            const size_t k = uniform(static_cast<uint32_t>(live.size()));
            const uint64_t id = live[k];
            live[k] = live.back();
            pos[live[k]] = static_cast<uint32_t>(k);
            live.pop_back();
            op.type = OpType::Cancel;
            op.id = id;
            ++st.cancels;
        } else if (r < static_cast<uint32_t>(cfg.add_pct) || want_cancel) {
            // (a cancel with nothing to cancel becomes an add)
            op.type = OpType::Add;
            op.side = uniform(2) ? Side::Sell : Side::Buy;
            op.qty = 1 + static_cast<int32_t>(uniform(100));
            if (op.side == Side::Buy) op.tick = kMinBidTick + static_cast<Tick>(uniform(cfg.band));
            else                      op.tick = kMaxAskTick - static_cast<Tick>(uniform(cfg.band));
            op.id = next_id++;
            pos[op.id] = static_cast<uint32_t>(live.size());
            live.push_back(op.id);
            ++st.adds;
        } else {
            op.type = OpType::Bbo;
            ++st.bbos;
        }
        ops.push_back(op);
    }
    st.live_at_end = live.size();
    if (stats) *stats = st;
    return ops;
}
