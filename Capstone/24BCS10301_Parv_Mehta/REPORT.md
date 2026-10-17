# Performance report

Parv Mehta, 24BCS10301

## 1. Setup and how to read the numbers

| | |
| --- | --- |
| Timing runs | Apple M5 Pro (macOS, arm64), Apple clang 21, `-std=c++17 -O2` |
| perf / cachegrind runs | same machine, Debian 12 container (Docker), g++ 12.2, `-O2 -g -fno-omit-frame-pointer`, Linux 7.0 (linuxkit) |
| Workload | 1,000,000 ops, `mt19937(42)`: 700,829 adds, 199,496 cancels, 99,675 BBO queries, 10,000 depth(10) queries, 501,333 orders live at the end |
| Repetitions | 5 per implementation, median reported |

Three things to keep in mind:

1. **Hardware counters were not available.** Docker's Linux VM on Apple
   Silicon does not expose the PMU, so `perf stat` printed `<not supported>`
   for cycles, instructions, branches, branch-misses, cache-misses,
   L1-dcache-load-misses and dTLB-load-misses. `task-clock` and
   `page-faults` worked, and `perf record` worked with the `cpu-clock`
   software event. For cache and branch behaviour I ran the same binary
   under **cachegrind** (`valgrind --tool=cachegrind --cache-sim=yes
   --branch-sim=yes`, 128 KB 8-way L1d / 16 MB LL / 64 B lines). Those are
   simulated counts and are marked *(sim)* below. IPC cannot be derived from
   a simulation and is left as n/a.
2. **A baseline is subtracted.** `./bench --impl none` runs the identical
   harness (workload generation, loop, checksum) against an empty book. Its
   counts are subtracted from A, B and C in the main table so that the
   numbers describe the order book and not the benchmark. Raw values are in
   the second table and in `perf/results/`.
3. **Per-op timers are coarse on this machine.** `steady_clock` ticks every
   41.7 ns on Apple Silicon, and the two clock reads around each call cost
   about 11.5 ns on average (that is what the `none` baseline measures per
   op). The whole-loop "total time" has none of that overhead and is the
   number I trust most; ns/op figures are averages over 10^5 to 7x10^5
   samples and are fine for comparing, but a single "12 ns" should be read
   as "well under one clock tick".

## 2. Results table

Book-only numbers (baseline subtracted where it applies). Timing from the
Mac, perf and cachegrind from the Linux container.

| Metric | A: std::map | B: price heap | C: arrays + bitmap |
| --- | ---: | ---: | ---: |
| Total time (ms), whole loop, median of 5 | **89.3** | **74.4** | **10.2** |
| ns / add (avg, timer overhead included) | 70.7 | 59.9 | 14.9 |
| ns / cancel | 150.3 | 156.7 | 13.9 |
| ns / BBO | 12.2 | 13.1 | 12.1 |
| ns / get_bids(10)+get_asks(10) | 222 | 1206 | 189 |
| add p99 / p99.9 / max (ns) | 167 / 292 / 5,823,584 | 250 / 375 / 5,044,792 | 42 / 83 / 17,375 |
| cancel p99 / p99.9 / max (ns) | 459 / 625 / 28,250 | 625 / 1000 / 31,958 | 42 / 84 / 4,125 |
| task-clock (ms, perf stat, Linux) | 123 | 127 | 16 |
| IPC | n/a (PMU not exposed in VM) | n/a | n/a |
| Instructions *(sim)* | 497 M | 637 M | 136 M |
| Branches *(sim)* | 80.8 M | 93.7 M | 13.8 M |
| Branch misses (%) *(sim)* | 8.2 M (**10.2%**) | 3.5 M (3.7%) | 0.70 M (5.0%) |
| L1-dcache-load-misses *(sim, D1 read misses)* | 7.94 M | 9.73 M | 1.13 M |
| Cache misses *(sim, last-level data misses)* | 1.43 M | 1.75 M | 0.75 M |
| Page faults (perf stat) | 7,395 | 8,200 | 3,595 |

Raw, un-subtracted values (`perf/results/perf_stat_*.txt`, `cachegrind_*.txt`):

| Metric | none (baseline) | A | B | C |
| --- | ---: | ---: | ---: | ---: |
| task-clock (ms) | 21.1 | 143.9 | 148.2 | 37.3 |
| page-faults | 8,950 | 16,345 | 17,150 | 12,545 |
| Ir, instructions *(sim)* | 186.9 M | 683.8 M | 824.3 M | 323.4 M |
| D1 misses (rd + wr) *(sim)* | 1.25 M | 9.94 M | 12.74 M | 2.64 M |
| LLd misses *(sim)* | 0.98 M | 2.41 M | 2.73 M | 1.72 M |
| branches / mispredicts *(sim)* | 15.4 M / 1.22 M | 96.2 M / 9.45 M | 109.1 M / 4.73 M | 29.2 M / 1.91 M |
| whole-loop time inside the container (ms, median) | 2.0 | 112 | 82 | 14.3 |

Correctness: `./bench --verify` reports PASS for all three and the checksum
`12959556190523934793` is identical for A, B, C in verify and in every
benchmark run (`results/verify.txt`, `results/bench_*.txt`).

## 3. Where the time goes (perf record, cpu-clock, 5 passes over the workload)

```
A                                         B                                         C
40.1%  MapBook::add_order                 28.0%  HeapBook::cancel_order             24.8%  ArrayBook::add_order
26.5%  MapBook::cancel_order              10.8%  HeapBook::add_order                19.5%  run_workload<ArrayBook>  (get_bbo/get_bids inlined here)
 5.4%  _int_free                           8.8%  _int_free                          15.4%  ArrayBook::cancel_order
 4.8%  unordered_map::_M_rehash            7.5%  unordered_map::_M_rehash            9.0%  generate_workload
 4.7%  malloc_consolidate                  7.0%  __memcpy_generic (heap copy)        3.1%  vector<Slot>::_M_fill_assign (table init)
 1.7%  malloc                              5.9%  malloc_consolidate                  3.5%  lround (double -> tick)
 1.6%  _int_malloc                         4.8%  _int_malloc
```

For A and B roughly 15-25% of all samples are inside glibc's allocator
(`_int_malloc`, `_int_free`, `malloc_consolidate`, `unlink_chunk`) plus the
`unordered_map` rehash. For C the allocator does not appear at all in the
book's code paths; what is left is the book itself, the harness, and the
one-off 16 MB table initialisation. Even `lround` (converting the API's
double price to a tick) becomes visible at 3.5% once everything else is
cheap.

## 4. Analysis

### 4.1 Which implementation is fastest?

C is fastest for all three operations, and by a wide margin for add and
cancel (about 5x and 11x on the averages, 9x on the whole-loop time). For
`get_bbo` all three land at the same ~12 ns, which is essentially the timer
tick: all three keep the best price ready (tree `begin()`, heap front, cached
index) so a BBO read is a couple of loads for everyone. The differences are in
add and cancel.

Why C wins:

- **No search.** A tick maps straight to an array index. A walks a
  red-black tree (~12 levels for 5000 live levels, each level a dependent
  pointer load); B hashes the price and then hashes the id as well.
- **No allocation.** A allocates a tree node per level and a hash node per
  order; B allocates a hash node per order plus a vector per level; C
  allocates nothing after construction. In the profiles the allocator alone
  costs A and B more than C spends in total.
- **Fewer instructions.** 136 M instructions for C against 497 M (A) and
  637 M (B) for the same work. Fewer instructions, fewer branches, fewer
  loads: everything downstream gets cheaper too.

B beats A on add (one hash insert plus, rarely, a heap push, instead of a
tree insert), but loses on cancel because it has to do two hash lookups and
then scan the level's vector (about 50 orders per level by the end of the
run), and it is 5x slower than the others on depth queries because listing
the top 10 of a heap means copying the whole heap (up to 5000 ints) and
popping 10 times; that copy is the `memcpy` at 7% of B's profile.

### 4.2 Branch prediction

A has by far the most mispredicted branches: 8.2 M, 10.2% of its branches,
versus 3.7% for B and 5.0% for C (cachegrind's simulated predictor). Per
function, 5.1 M of A's mispredicts are in the `std::map` code inlined into
`add_order` and another 1.4 M in `cancel_order`, i.e. in the red-black tree
walk. Each step of the walk is "is the key smaller than this node's key,
go left or right?". With uniformly random prices the answer is a coin flip
at every node, so the predictor cannot learn a pattern; a 12-level descent
with a ~50% mispredict chance per level is the worst case for the branch
unit. The rebalancing after insert/erase adds more data-dependent branches.

B's branches are mostly in the hash tables and the per-level vector scan,
which are loops with a predictable "keep going" pattern, so its rate is
lowest. C's absolute count is the smallest (0.7 M) because it executes the
fewest branches; its rate (5%) is dominated by the hash-table probe loop
(`while (slot.id != id)`), whose trip count varies, and by the "did this
level just become empty / is it the best" checks, which are genuinely
data-dependent.

### 4.3 Cache behaviour

B has the most cache misses (9.7 M L1 read misses, 1.75 M last-level),
closely followed by A (7.9 M / 1.43 M); C has 1.1 M / 0.75 M.

- **A follows pointers and allocates many small objects.** Every tree node
  and every `unordered_map` node is a separate `malloc`, placed wherever the
  allocator had a free chunk. Walking the tree is a chain of dependent loads
  to unrelated cache lines, and the hash lookup is bucket array -> node
  pointer -> node, another two lines. cachegrind puts 4.0 M of A's D1 read
  misses inside the `stl_tree.h` code of `add_order`.
- **B jumps around memory and copies.** Two hash tables (ids and levels)
  give two node-chasing lookups per operation, each level's order vector is
  yet another heap allocation, and the depth queries memcpy ~20 KB and then
  pop from it (1.57 M D1 misses attributed to `memcpy`). B also has the most
  write misses: it writes 50% more data than A.
- **C accesses contiguous memory.** The 20 KB level arrays and the 650 byte
  bitmaps stay in L1/L2 for the whole run. The only cold structure is the
  16 MB order table: a random id hashes to a random cache line, so an add or
  cancel costs about one L1 miss that usually hits L2/LL. That is visible
  as 841 K D1 misses in `ArrayBook::add_order`, which is nearly all of C's
  miss count, and is also why C's LL miss *rate* (1.4%) is higher than A's
  (0.8%): C makes four times fewer memory accesses in total, so the
  unavoidable table misses are a bigger share of a much smaller number.

The allocator itself is another cache cost for A and B: `_int_free` and
`malloc_consolidate` touch free-list metadata all over the heap (170-820 K
D1 misses each in the cachegrind breakdown).

### 4.4 Page faults

B causes the most page faults (8,200 above baseline), A is close (7,395), C
is less than half (3,595). A page fault happens the first time a process
touches a page, so this is a measure of how much *new* memory each book
grows into during the run. A and B hold ~500 K live orders in
`unordered_map` nodes (about 48 bytes each including malloc overhead) plus
their bucket arrays, which `_M_rehash` keeps reallocating as the table grows
(the 5 ms `max` latency for add in both A and B is exactly one of those
rehashes); B also has a second hash table and one vector per level. The
~30 MB of growth arrives in small pieces throughout the run, so the faults
are spread across the trading path. Most of C's faults are the 16 MB order table
being zero-filled in the constructor (16 MB is 4096 pages of 4 KB; the count
comes out lower because the allocator reuses pages the workload generator
had already touched and freed). Either way the memory is touched once, up
front, and never again. In a trading system that is the behaviour
you want, because page faults then happen before the market opens instead of
on a live order.

### 4.5 Production choice

I would deploy C, but not only because its average latency is lowest.

- **Latency and predictability.** C's add/cancel p99.9 is 83-84 ns against
  292-1000 ns for A and B, and its worst observed operation is 17 us versus
  5-6 ms. The 5 ms spikes in A and B are `unordered_map` rehashes, which in
  a real system would land on a random order at a random time. C has no
  operation whose cost depends on how many orders are in the book (bounded
  probe length at <75% load, at most 3 words of bitmap). That said, C's
  table *can* still grow if the live count exceeds what it was sized for, so
  in production I would size it for the exchange's maximum and treat growth
  as an error.
- **Memory.** C uses a fixed 16 MB + 40 KB regardless of the number of
  orders, which is more than A/B at low load but less at high load, and
  more importantly it is constant and known in advance.
- **Cache behaviour.** Covered above: contiguous arrays, one cold line per
  op, no allocator traffic.
- **Complexity and maintainability.** This is where C costs something. The
  bitmap and the open-addressing table with backward-shift deletion are about
  150 lines that need unit tests (word-boundary cases, table growth) and a
  careful reviewer; A is 90 lines anyone can read. C also bakes in the
  fixed price band from the assignment; a real instrument whose price can
  drift outside the band would need the array re-centred or a sparse
  fallback, which A and B get for free. Given that the data structure is
  small, self-contained, and covered by tests that compare it against a
  brute-force oracle and against the other two books, I think the extra
  review cost is well spent for the latency profile it buys.

I would not pick B. It is only marginally faster than A on average, its
cancel path is the slowest, its depth queries are 5x slower, and the lazy
deletion makes its behaviour harder to reason about than either of the
others.

### 4.6 What I would try next

- **Integer-tick API.** `lround(price * 100)` shows up at 3.5% of C's
  profile. Passing ticks in and out would remove it.
- **Direct-indexed order slots.** Order ids in this workload are sequential.
  `orders[id]` in a flat vector would turn the hash lookup into one load,
  no hashing, no probing; with a free-list it would also work for reused
  slots. That would remove most of C's remaining cache misses.
- **Smaller order slots / hugepages.** 16-byte slots at 50% load mean 32
  bytes of table per order. Packing qty and tick into 8 bytes, or backing
  the table with 2 MB hugepages, would cut dTLB misses on the table.
- **Reserve in A and B.** Calling `reserve()` on the `unordered_map`s would
  remove the 5 ms rehash spikes. It would not fix the per-node allocation.
- **Pool allocator for A.** A `std::map` with a custom pool allocator keeps
  the tree but puts nodes in a contiguous arena, which should recover a lot
  of its cache misses without changing the design.
- **Intrusive per-level order lists for B/C.** If a matching engine were
  added later, orders would need price-time priority; an intrusive doubly
  linked list threaded through the order slots gives that in O(1) with no
  extra allocation.
- **Branchless probe / prefetch.** On cancel, prefetch the order's table
  slot as soon as the id is known and before doing anything else.
- **Real hardware counters.** Run `perf/run_perf.sh` on a bare-metal Linux
  machine to replace the simulated cache/branch numbers with measured ones
  and to get IPC.

## Appendix: perf stat output

`perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults`
inside the Linux container. Full files, including the benchmark's own
output for each of the 5 runs, are in `perf/results/perf_stat_{A,B,C,none}.txt`.

### Implementation A
```
 Performance counter stats for 'build-linux/bench --impl A --timing total --ops 1000000' (5 runs):

            143.91 msec task-clock                       #    1.034 CPUs utilized            ( +-  4.89% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             16345      page-faults                      #  118.853 K/sec                    ( +-  0.00% )

           0.13924 +- 0.00709 seconds time elapsed  ( +-  5.09% )

```

### Implementation B
```
 Performance counter stats for 'build-linux/bench --impl B --timing total --ops 1000000' (5 runs):

            148.15 msec task-clock                       #    1.122 CPUs utilized            ( +-  5.17% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             17150      page-faults                      #  131.996 K/sec                    ( +-  0.00% )

           0.13205 +- 0.00761 seconds time elapsed  ( +-  5.76% )

```

### Implementation C
```
 Performance counter stats for 'build-linux/bench --impl C --timing total --ops 1000000' (5 runs):

             37.26 msec task-clock                       #    0.925 CPUs utilized            ( +-  3.25% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             12545      page-faults                      #  315.988 K/sec                    ( +-  0.81% )

           0.04029 +- 0.00125 seconds time elapsed  ( +-  3.10% )

```

### Baseline (`--impl none`, no order book)
```
 Performance counter stats for 'build-linux/bench --impl none --timing total --ops 1000000' (5 runs):

             21.05 msec task-clock                       #    0.957 CPUs utilized            ( +-  1.63% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
              8950      page-faults                      #  416.856 K/sec                    ( +-  0.00% )

          0.022004 +- 0.000380 seconds time elapsed  ( +-  1.73% )

```
