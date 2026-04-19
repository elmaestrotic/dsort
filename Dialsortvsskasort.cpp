/**
 * DialSort vs. ska_sort — Direct Comparison Benchmark
 * ====================================================
 * Paper: "DialSort: Non-Comparative Integer Sorting via the
 *         Self-Indexing Principle"
 * Author: Alexander Narvaez  |  Independent Researcher
 *         Universidad EAFIT, Envigado, Antioquia, Colombia
 *
 * PURPOSE
 * -------
 * Side-by-side benchmark of DialSort, DialSort-Parallel (8 threads),
 * and ska_sort across all experimental dimensions used in the paper.
 *
 * DEPENDENCY
 * ----------
 * ska_sort is a header-only library.
 *   git clone https://github.com/skarupke/ska_sort
 *   Then compile with: -I./ska_sort
 *
 * WHAT IS MEASURED
 * ----------------
 *   1. DialSort            : 2 passes, O(U) memory, NO prefix sum, in-place
 *   2. DialSort-Parallel   : same as above, 8 threads (parallel ingestion)
 *   3. ska_sort            : non-comparative radix sort (American flag sort)
 *   4. std::sort           : GCC introsort baseline
 *
 * DISTRIBUTIONS
 * -------------
 *   uniform : keys in [0, U-1]
 *   skewed  : 80% keys in bottom 5% of universe
 *   sorted  : ascending
 *   reverse : descending
 *
 * REPRODUCIBILITY
 * ---------------
 *   Seed    : fixed (20260321)
 *   Timing  : best-of-7 runs, 3 warmup discarded
 *   Correctness: check_sorted() after every run
 *
 * COMPILE
 * -------
 *   g++ -O3 -std=c++17 -pthread -I./ska_sort -o bench_ska DialsortVsSkaSort.cpp
 *
 * To exclude ska_sort (e.g. if not yet cloned):
 *   g++ -O3 -std=c++17 -pthread -DSKIP_SKA -o bench_ska DialsortVsSkaSort.cpp
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifndef SKIP_SKA
#include "ska_sort.hpp"
#endif

// ── Parameters ────────────────────────────────────────────────────────────────
static constexpr int      WARMUP_ROUNDS  = 3;
static constexpr int      MEASURE_ROUNDS = 7;
static constexpr long     SEED           = 20260321L;
static constexpr uint64_t MAX_U          = 10'000'000ULL;
static constexpr int      NUM_THREADS    = 8;

// ── Timer ─────────────────────────────────────────────────────────────────────
static inline int64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
            high_resolution_clock::now().time_since_epoch()).count();
}

// ── Correctness ───────────────────────────────────────────────────────────────
static bool check_sorted(const std::vector<int>& a) {
    for (size_t i = 1; i < a.size(); ++i)
        if (a[i-1] > a[i]) return false;
    return true;
}

// ── Universe-size guard ───────────────────────────────────────────────────────
static std::pair<bool, uint64_t> universe_size(int mn, int mx) {
    const uint64_t U = static_cast<uint64_t>(
                               static_cast<int64_t>(mx) - static_cast<int64_t>(mn)
                       ) + 1ULL;
    return {U <= MAX_U, U};
}

// ════════════════════════════════════════════════════════════════════════════════
//  ALGORITHM 1 — DialSort (sequential)
//  Passes: 2  |  Memory: O(U)  |  Prefix sum: NONE
// ════════════════════════════════════════════════════════════════════════════════
static bool dialsort(std::vector<int>& a) {
    const size_t n = a.size();
    if (n <= 1) return true;

    int mn = a[0], mx = a[0];
    for (size_t i = 1; i < n; ++i) {
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }

    auto [ok, U64] = universe_size(mn, mx);
    if (!ok) { std::cerr << "[WARN] dialsort: U > MAX_U\n"; return false; }
    const size_t U = static_cast<size_t>(U64);

    // Pass 1 — Ingestion
    std::vector<int> H(U, 0);
    for (size_t i = 0; i < n; ++i)
        H[static_cast<size_t>(a[i] - mn)]++;

    // Pass 2 — Projection (in-place, no prefix sum)
    size_t out = 0;
    for (size_t y = 0; y < U; ++y) {
        const int val = static_cast<int>(y) + mn;
        for (int c = H[y]; c > 0; --c)
            a[out++] = val;
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════════════════
//  ALGORITHM 2 — DialSort-Parallel (multi-threaded ingestion)
//  Each thread builds a private partial histogram; merge step is additive.
//  Projection is sequential (single geometric scan of H).
// ════════════════════════════════════════════════════════════════════════════════
static bool dialsort_parallel(std::vector<int>& a, int nthreads = NUM_THREADS) {
    const size_t n = a.size();
    if (n <= 1) return true;

    int mn = a[0], mx = a[0];
    for (size_t i = 1; i < n; ++i) {
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }

    auto [ok, U64] = universe_size(mn, mx);
    if (!ok) { std::cerr << "[WARN] dialsort_parallel: U > MAX_U\n"; return false; }
    const size_t U = static_cast<size_t>(U64);

    const int hw = static_cast<int>(std::thread::hardware_concurrency());
    const int nt = std::max(1, std::min(nthreads, hw > 0 ? hw : nthreads));

    std::vector<std::vector<int>> local_H(nt, std::vector<int>(U, 0));

    std::vector<std::thread> workers;
    const size_t chunk = (n + nt - 1) / nt;
    for (int t = 0; t < nt; ++t) {
        workers.emplace_back([&, t]() {
            const size_t lo = t * chunk;
            const size_t hi = std::min(lo + chunk, n);
            auto& lh = local_H[t];
            for (size_t i = lo; i < hi; ++i)
                lh[static_cast<size_t>(a[i] - mn)]++;
        });
    }
    for (auto& w : workers) w.join();

    std::vector<int> H(U, 0);
    for (int t = 0; t < nt; ++t)
        for (size_t y = 0; y < U; ++y)
            H[y] += local_H[t][y];

    size_t out = 0;
    for (size_t y = 0; y < U; ++y) {
        const int val = static_cast<int>(y) + mn;
        for (int c = H[y]; c > 0; --c)
            a[out++] = val;
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════════════════
//  DATA GENERATORS
// ════════════════════════════════════════════════════════════════════════════════
static std::vector<int> gen_uniform(size_t n, int U, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> d(0, U - 1);
    std::vector<int> a(n);
    for (auto& x : a) x = d(rng);
    return a;
}

static std::vector<int> gen_skewed(size_t n, int U, uint64_t seed) {
    std::mt19937_64 rng(seed);
    const int hot_limit = std::max(1, U / 20);
    std::uniform_int_distribution<int> hot(0, hot_limit - 1);
    std::uniform_int_distribution<int> cold(0, U - 1);
    std::bernoulli_distribution pick_hot(0.80);
    std::vector<int> a(n);
    for (auto& x : a) x = pick_hot(rng) ? hot(rng) : cold(rng);
    return a;
}

static std::vector<int> gen_sorted(size_t n, int U, uint64_t seed) {
    auto a = gen_uniform(n, U, seed);
    std::sort(a.begin(), a.end());
    return a;
}

static std::vector<int> gen_reverse(size_t n, int U, uint64_t seed) {
    auto a = gen_sorted(n, U, seed);
    std::reverse(a.begin(), a.end());
    return a;
}

// ════════════════════════════════════════════════════════════════════════════════
//  BENCHMARK HARNESS
// ════════════════════════════════════════════════════════════════════════════════
using SortFn = std::function<bool(std::vector<int>&)>;

struct Row {
    std::string algo;
    std::string dist;
    size_t      n       = 0;
    int         U       = 0;
    double      ms      = 0;
    double      mkeys_s = 0;
    double      speedup = 0;
    bool        correct = false;
    bool        skipped = false;
};

static Row run_one(const std::string& algo,
                   const std::string& dist,
                   const std::vector<int>& base,
                   int U,
                   SortFn fn,
                   double baseline_ms)
{
    Row row;
    row.algo = algo;
    row.dist = dist;
    row.n    = base.size();
    row.U    = U;

    for (int r = 0; r < WARMUP_ROUNDS; ++r) {
        auto tmp = base;
        if (!fn(tmp)) { row.skipped = true; return row; }
    }

    int64_t best = INT64_MAX;
    bool ok = true;

    for (int r = 0; r < MEASURE_ROUNDS; ++r) {
        auto tmp = base;
        const int64_t t0 = now_ns();
        if (!fn(tmp)) { row.skipped = true; return row; }
        const int64_t elapsed = now_ns() - t0;
        if (elapsed > 0 && elapsed < best) best = elapsed;
        if (!check_sorted(tmp)) { ok = false; break; }
    }

    if (best == INT64_MAX) { row.skipped = true; return row; }

    row.ms      = best / 1e6;
    row.mkeys_s = (base.size() / (best / 1e9)) / 1e6;
    row.speedup = (baseline_ms > 0) ? baseline_ms / row.ms : 1.0;
    row.correct = ok;
    return row;
}

// ── Printing ──────────────────────────────────────────────────────────────────
static constexpr int COL_W = 110;
static void separator() { std::cout << std::string(COL_W, '-') << "\n"; }

static void print_table_header() {
    std::cout << std::left
              << std::setw(28) << "Algorithm"
              << std::setw(10) << "Dist"
              << std::setw(12) << "N"
              << std::setw(8)  << "U"
              << std::setw(13) << "ms (best)"
              << std::setw(14) << "M keys/s"
              << std::setw(14) << "vs std::sort"
              << "OK\n";
    separator();
}

static void print_row(const Row& r) {
    if (r.skipped) {
        std::cout << std::left << std::setw(28) << r.algo
                  << std::setw(10) << r.dist
                  << std::setw(12) << r.n
                  << std::setw(8)  << r.U
                  << "[SKIPPED]\n";
        return;
    }
    std::cout << std::left
              << std::setw(28) << r.algo
              << std::setw(10) << r.dist
              << std::setw(12) << r.n
              << std::setw(8)  << r.U
              << std::fixed << std::setprecision(3)
              << std::setw(13) << r.ms
              << std::setw(14) << r.mkeys_s
              << std::setw(14) << r.speedup
              << (r.correct ? "OK" : "*** FAIL ***") << "\n";
}

static void csv_row(const Row& r) {
    if (r.skipped) {
        std::cout << r.algo << "," << r.dist << "," << r.n << "," << r.U
                  << ",SKIPPED,SKIPPED,SKIPPED,SKIPPED\n";
        return;
    }
    std::cout << r.algo    << ","
              << r.dist    << ","
              << r.n       << ","
              << r.U       << ","
              << std::fixed << std::setprecision(3)
              << r.ms      << ","
              << r.mkeys_s << ","
              << r.speedup << ","
              << (r.correct ? "OK" : "FAIL") << "\n";
}

// ════════════════════════════════════════════════════════════════════════════════
//  MAIN
// ════════════════════════════════════════════════════════════════════════════════
int main() {
    std::cout
            << "================================================================\n"
            << "DialSort vs. ska_sort — Direct Comparison Benchmark\n"
            << "Paper: DialSort: Non-Comparative Integer Sorting\n"
            << "       via the Self-Indexing Principle\n"
            << "================================================================\n"
            << "Compiler     : g++ " << __VERSION__ << "\n"
            << "Flags        : -O3 -std=c++17 -pthread\n"
            << "Threads      : " << NUM_THREADS << "\n"
            << "Warmup       : " << WARMUP_ROUNDS << " discarded runs\n"
            << "Measurement  : best-of-" << MEASURE_ROUNDS << " runs\n"
            << "Seed         : " << SEED << "\n"
            #ifdef SKIP_SKA
            << "ska_sort     : NOT COMPILED (recompile without -DSKIP_SKA)\n"
            #else
            << "ska_sort     : ENABLED\n"
            #endif
            << "================================================================\n\n";

    const std::vector<size_t> Ns = {10'000, 100'000, 1'000'000, 10'000'000};
    const std::vector<int>    Us = {256, 1024, 65536};

    using GenFn = std::vector<int>(*)(size_t, int, uint64_t);
    struct Dist { std::string name; GenFn gen; };
    const std::vector<Dist> dists = {
            {"uniform", gen_uniform},
            {"skewed",  gen_skewed},
            {"sorted",  gen_sorted},
            {"reverse", gen_reverse},
    };

    SortFn fn_dialsort = [](std::vector<int>& a){ return dialsort(a); };
    SortFn fn_parallel = [](std::vector<int>& a){ return dialsort_parallel(a); };
    SortFn fn_std      = [](std::vector<int>& a){ std::sort(a.begin(), a.end()); return true; };

#ifndef SKIP_SKA
    // ska_sort sorts in-place and supports int natively.
    // ska_sort_copy requires a destination buffer; we use ska_sort (in-place American flag sort).
    SortFn fn_ska = [](std::vector<int>& a){
        ska_sort(a.begin(), a.end());
        return true;
    };
#endif

    std::cout << "TABLE — DialSort vs ska_sort vs std::sort\n";
    std::cout << "Column 'vs std::sort': speedup relative to GCC introsort\n\n";
    print_table_header();

    struct GroupResult {
        Row dialsort, parallel, ska, std_sort;
    };
    std::vector<GroupResult> results;

    for (size_t n : Ns) {
        for (int U : Us) {
            for (const auto& dist : dists) {
                const uint64_t seed = static_cast<uint64_t>(SEED)
                                      ^ (static_cast<uint64_t>(n) * 1000003ULL)
                                      ^ (static_cast<uint64_t>(U) * 7919ULL)
                                      ^ 0xC0FFEEULL;

                const auto base = dist.gen(n, U, seed);

                auto r_std = run_one("std::sort", dist.name, base, U, fn_std, 0.0);
                r_std.speedup = 1.0;
                const double baseline_ms = r_std.ms;

                auto r_dialsort = run_one("DialSort",          dist.name, base, U, fn_dialsort, baseline_ms);
                auto r_parallel = run_one("DialSort-Parallel", dist.name, base, U, fn_parallel, baseline_ms);

#ifndef SKIP_SKA
                auto r_ska = run_one("ska_sort", dist.name, base, U, fn_ska, baseline_ms);
#endif

                print_row(r_std);
                print_row(r_dialsort);
                print_row(r_parallel);
#ifndef SKIP_SKA
                print_row(r_ska);
#endif
                std::cout << "\n";

                GroupResult g;
                g.std_sort = r_std;
                g.dialsort = r_dialsort;
                g.parallel = r_parallel;
#ifndef SKIP_SKA
                g.ska      = r_ska;
#endif
                results.push_back(g);
            }
        }
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    std::cout << "================================================================\n";
    std::cout << "SUMMARY — DialSort vs ska_sort\n";
    std::cout << "================================================================\n\n";

#ifndef SKIP_SKA
    int ds_beats_ska = 0, total = 0;
    double sum_ratio = 0;
    double min_ratio = 1e9, max_ratio = 0;

    for (const auto& g : results) {
        if (g.dialsort.skipped || g.ska.skipped) continue;
        ++total;
        const double ratio = g.dialsort.ms / g.ska.ms;
        sum_ratio += ratio;
        min_ratio = std::min(min_ratio, ratio);
        max_ratio = std::max(max_ratio, ratio);
        if (ratio < 1.0) ++ds_beats_ska;
    }

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Configurations measured              : " << total << "\n"
              << "DialSort faster than ska_sort        : " << ds_beats_ska << " / " << total << "\n"
              << "Avg ratio DialSort / ska_sort         : " << (total > 0 ? sum_ratio / total : 0) << "x\n"
              << "Min ratio DialSort / ska_sort (best)  : " << min_ratio << "x\n"
              << "Max ratio DialSort / ska_sort (worst) : " << max_ratio << "x\n\n"
              << "NOTE: ratio < 1.0 => DialSort is faster than ska_sort\n"
              << "      ratio > 1.0 => ska_sort is faster than DialSort\n\n";
#else
    std::cout << "ska_sort not compiled. Recompile without -DSKIP_SKA to see comparison.\n\n";
#endif

    bool all_ok = true;
    for (const auto& g : results) {
        if (!g.dialsort.skipped && !g.dialsort.correct) { all_ok = false; break; }
        if (!g.parallel.skipped && !g.parallel.correct) { all_ok = false; break; }
#ifndef SKIP_SKA
        if (!g.ska.skipped && !g.ska.correct) { all_ok = false; break; }
#endif
    }
    std::cout << "All correctness checks: " << (all_ok ? "PASSED" : "*** FAILURES ***") << "\n\n";

    // ── CSV ───────────────────────────────────────────────────────────────────
    std::cout << "================================================================\n";
    std::cout << "CSV OUTPUT\n";
    std::cout << "================================================================\n";
    std::cout << "algo,dist,N,U,ms_best,Mkeys_per_s,speedup_vs_std,correct\n";
    for (const auto& g : results) {
        csv_row(g.std_sort);
        csv_row(g.dialsort);
        csv_row(g.parallel);
#ifndef SKIP_SKA
        csv_row(g.ska);
#endif
    }

    return 0;
}