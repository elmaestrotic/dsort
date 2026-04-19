/**
 * DialSort vs. IPS4o — Sequential AND Parallel Comparison Benchmark
 * ==================================================================
 * Paper: "DialSort: Non-Comparative Integer Sorting via the
 *         Self-Indexing Principle"
 * Author: Alexander Narvaez  |  Independent Researcher
 *         Universidad EAFIT, Envigado, Antioquia, Colombia
 *
 * PURPOSE
 * -------
 * Adds IPS4o-parallel (TBB) to the existing sequential benchmark,
 * enabling a fair apples-to-apples comparison:
 *
 *   Sequential regime : DialSort       vs IPS4o-seq
 *   Parallel  regime  : DialSort-Par   vs IPS4o-par  (8 threads each)
 *
 * COMPILE (Linux/WSL — recommended for TBB)
 * ------------------------------------------
 *   g++ -O3 -std=c++17 -pthread \
 *       -I./ips4o/include \
 *       -ltbb \
 *       -o bench_parallel DialsortVsIps4oParallel.cpp
 *
 * COMPILE (fallback: TBB not available — disables IPS4o-parallel)
 * ----------------------------------------------------------------
 *   g++ -O3 -std=c++17 -pthread \
 *       -I./ips4o/include \
 *       -DSKIP_IPS4O_PARALLEL \
 *       -o bench_parallel DialsortVsIps4oParallel.cpp
 *
 * TBB CHECK (run first)
 * ----------------------
 *   pkg-config --modversion tbb   # should print version, e.g. 2021.x
 *   # If missing:
 *   sudo apt install libtbb-dev   # Debian/Ubuntu
 *   brew install tbb              # macOS
 *
 * METHODOLOGY (identical to existing benchmark suite)
 * ---------------------------------------------------
 *   Seed    : 20260321 (XOR-mixed per configuration)
 *   Warmup  : 3 discarded rounds
 *   Timing  : best-of-7 runs (nanosecond precision)
 *   Correct : check_sorted() after every run
 *   Threads : 8 (both DialSort-Parallel and IPS4o-parallel)
 *
 * WHAT IS MEASURED
 * ----------------
 *   1. std::sort          — GCC introsort baseline
 *   2. DialSort           — sequential, 2 passes, O(U) memory, no prefix sum
 *   3. DialSort-Parallel  — parallel ingestion, 8 threads
 *   4. IPS4o-seq          — sequential comparison-based (existing result)
 *   5. IPS4o-par          — parallel TBB, 8 threads  ← NEW
 *
 * FAIR COMPARISON NOTE (paper §10)
 * ---------------------------------
 *   Sequential: DialSort vs IPS4o-seq   (both single-threaded)
 *   Parallel  : DialSort-Par vs IPS4o-par (both 8 threads)
 *   Cross-regime comparisons are labeled but NOT used for win/loss counts.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

// IPS4o — header-only
#include "ips4o/include/ips4o.hpp"

#ifndef SKIP_IPS4O_PARALLEL
// IPS4o parallel requires TBB.
// If this include fails: compile with -DSKIP_IPS4O_PARALLEL
#include "ips4o/include/ips4o/parallel.hpp"
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
                               static_cast<int64_t>(mx) - static_cast<int64_t>(mn)) + 1ULL;
    return {U <= MAX_U, U};
}

// ════════════════════════════════════════════════════════════════════════════════
//  ALGORITHM 1 — DialSort (sequential)
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

    std::vector<int> H(U, 0);
    for (size_t i = 0; i < n; ++i)
        H[static_cast<size_t>(a[i] - mn)]++;

    size_t out = 0;
    for (size_t y = 0; y < U; ++y) {
        const int val = static_cast<int>(y) + mn;
        for (int c = H[y]; c > 0; --c)
            a[out++] = val;
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════════════════
//  ALGORITHM 2 — DialSort-Parallel (multi-threaded ingestion, 8 threads)
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
//  DATA GENERATORS  (identical to existing suite)
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
    size_t      n        = 0;
    int         U        = 0;
    double      ms_best  = 0;   // best-of-7
    double      ms_mean  = 0;   // mean of 7
    double      ms_std   = 0;   // std dev of 7
    double      mkeys_s  = 0;
    double      speedup  = 0;
    bool        correct  = true;
    bool        skipped  = false;
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

    // Warmup
    for (int r = 0; r < WARMUP_ROUNDS; ++r) {
        auto tmp = base;
        if (!fn(tmp)) { row.skipped = true; return row; }
    }

    // Measurement — collect all 7 times for mean/std
    std::vector<double> times_ms;
    times_ms.reserve(MEASURE_ROUNDS);
    int64_t best_ns = INT64_MAX;
    bool ok = true;

    for (int r = 0; r < MEASURE_ROUNDS; ++r) {
        auto tmp = base;
        const int64_t t0 = now_ns();
        if (!fn(tmp)) { row.skipped = true; return row; }
        const int64_t elapsed = now_ns() - t0;
        if (elapsed > 0) {
            times_ms.push_back(elapsed / 1e6);
            if (elapsed < best_ns) best_ns = elapsed;
        }
        if (!check_sorted(tmp)) { ok = false; break; }
    }

    if (best_ns == INT64_MAX || times_ms.empty()) { row.skipped = true; return row; }

    // Statistics
    const double mean = std::accumulate(times_ms.begin(), times_ms.end(), 0.0)
                        / times_ms.size();
    double sq_sum = 0;
    for (double t : times_ms) sq_sum += (t - mean) * (t - mean);
    const double stdev = times_ms.size() > 1
                         ? std::sqrt(sq_sum / (times_ms.size() - 1))
                         : 0.0;

    row.ms_best = best_ns / 1e6;
    row.ms_mean = mean;
    row.ms_std  = stdev;
    row.mkeys_s = (base.size() / (best_ns / 1e9)) / 1e6;
    row.speedup = (baseline_ms > 0) ? baseline_ms / row.ms_best : 1.0;
    row.correct = ok;
    return row;
}

// ── Printing ──────────────────────────────────────────────────────────────────
static constexpr int COL_W = 130;
static void separator() { std::cout << std::string(COL_W, '-') << "\n"; }

static void print_table_header() {
    std::cout << std::left
              << std::setw(26) << "Algorithm"
              << std::setw(10) << "Dist"
              << std::setw(12) << "N"
              << std::setw(8)  << "U"
              << std::setw(13) << "best(ms)"
              << std::setw(11) << "mean(ms)"
              << std::setw(11) << "std(ms)"
              << std::setw(14) << "M keys/s"
              << std::setw(14) << "vs std::sort"
              << "OK\n";
    separator();
}

static void print_row(const Row& r) {
    if (r.skipped) {
        std::cout << std::left << std::setw(26) << r.algo
                  << std::setw(10) << r.dist
                  << std::setw(12) << r.n
                  << std::setw(8)  << r.U
                  << "[SKIPPED]\n";
        return;
    }
    std::cout << std::left
              << std::setw(26) << r.algo
              << std::setw(10) << r.dist
              << std::setw(12) << r.n
              << std::setw(8)  << r.U
              << std::fixed << std::setprecision(3)
              << std::setw(13) << r.ms_best
              << std::setw(11) << r.ms_mean
              << std::setw(11) << r.ms_std
              << std::setw(14) << r.mkeys_s
              << std::setw(14) << r.speedup
              << (r.correct ? "OK" : "*** FAIL ***") << "\n";
}

static void csv_row(const Row& r) {
    if (r.skipped) {
        std::cout << r.algo << "," << r.dist << "," << r.n << "," << r.U
                  << ",SKIPPED,SKIPPED,SKIPPED,SKIPPED,SKIPPED,SKIPPED\n";
        return;
    }
    std::cout << r.algo    << ","
              << r.dist    << ","
              << r.n       << ","
              << r.U       << ","
              << std::fixed << std::setprecision(3)
              << r.ms_best << ","
              << r.ms_mean << ","
              << r.ms_std  << ","
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
            << "DialSort vs. IPS4o — Sequential + Parallel Benchmark\n"
            << "Paper: DialSort: Non-Comparative Integer Sorting\n"
            << "       via the Self-Indexing Principle\n"
            << "================================================================\n"
            << "Compiler     : g++ " << __VERSION__ << "\n"
            << "Flags        : -O3 -std=c++17 -pthread -ltbb\n"
            << "Threads      : " << NUM_THREADS << "\n"
            << "Warmup       : " << WARMUP_ROUNDS << " discarded runs\n"
            << "Measurement  : best-of-" << MEASURE_ROUNDS
            << " + mean + std dev reported\n"
            << "Seed         : " << SEED << "\n"
            #ifdef SKIP_IPS4O_PARALLEL
            << "IPS4o-par    : DISABLED (compiled with -DSKIP_IPS4O_PARALLEL)\n"
            #else
            << "IPS4o-par    : ENABLED (" << NUM_THREADS << " TBB threads)\n"
            #endif
            << "================================================================\n\n"
            << "FAIR COMPARISON NOTE\n"
            << "  Sequential regime : DialSort       vs IPS4o-seq  (both 1 thread)\n"
            << "  Parallel  regime  : DialSort-Par   vs IPS4o-par  (both "
            << NUM_THREADS << " threads)\n"
            << "  Cross-regime rows are shown for reference only.\n"
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
    SortFn fn_std      = [](std::vector<int>& a){
        std::sort(a.begin(), a.end()); return true; };
    SortFn fn_ips4o_seq = [](std::vector<int>& a){
        ips4o::sort(a.begin(), a.end()); return true; };

#ifndef SKIP_IPS4O_PARALLEL
    SortFn fn_ips4o_par = [](std::vector<int>& a){
        ips4o::parallel::sort(a.begin(), a.end()); return true; };
#endif

    std::cout << "TABLE — All algorithms, all configurations\n";
    std::cout << "Columns: best(ms) | mean(ms) | std(ms) | M keys/s | vs std::sort\n\n";
    print_table_header();

    struct GroupResult {
        Row std_sort, dialsort, parallel, ips4o_seq;
#ifndef SKIP_IPS4O_PARALLEL
        Row ips4o_par;
#endif
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
                const double baseline_ms = r_std.ms_best;

                auto r_ds  = run_one("DialSort",
                                     dist.name, base, U, fn_dialsort, baseline_ms);
                auto r_par = run_one("DialSort-Parallel",
                                     dist.name, base, U, fn_parallel, baseline_ms);
                auto r_seq = run_one("IPS4o-seq",
                                     dist.name, base, U, fn_ips4o_seq, baseline_ms);
#ifndef SKIP_IPS4O_PARALLEL
                auto r_pips = run_one("IPS4o-par",
                                      dist.name, base, U, fn_ips4o_par, baseline_ms);
#endif

                // Print group with visual separator between regimes
                std::cout << "── seq ──\n";
                print_row(r_std);
                print_row(r_ds);
                print_row(r_seq);
                std::cout << "── par ──\n";
                print_row(r_par);
#ifndef SKIP_IPS4O_PARALLEL
                print_row(r_pips);
#endif
                std::cout << "\n";

                GroupResult g;
                g.std_sort  = r_std;
                g.dialsort  = r_ds;
                g.parallel  = r_par;
                g.ips4o_seq = r_seq;
#ifndef SKIP_IPS4O_PARALLEL
                g.ips4o_par = r_pips;
#endif
                results.push_back(g);
            }
        }
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    std::cout << "================================================================\n"
              << "SUMMARY\n"
              << "================================================================\n\n";

    // Sequential: DialSort vs IPS4o-seq
    {
        int wins = 0, total = 0;
        double sum_ratio = 0, min_r = 1e9, max_r = 0;
        for (const auto& g : results) {
            if (g.dialsort.skipped || g.ips4o_seq.skipped) continue;
            ++total;
            const double r = g.dialsort.ms_best / g.ips4o_seq.ms_best;
            sum_ratio += r; min_r = std::min(min_r, r); max_r = std::max(max_r, r);
            if (r < 1.0) ++wins;
        }
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "[SEQUENTIAL] DialSort vs IPS4o-seq\n"
                  << "  Configs     : " << total << "\n"
                  << "  DS faster   : " << wins << " / " << total << "\n"
                  << "  Avg ratio   : " << (total > 0 ? sum_ratio/total : 0)
                  << "  (< 1.0 = DS faster)\n"
                  << "  Best ratio  : " << min_r << "\n"
                  << "  Worst ratio : " << max_r << "\n\n";
    }

#ifndef SKIP_IPS4O_PARALLEL
    // Parallel: DialSort-Par vs IPS4o-par
    {
        int wins = 0, total = 0;
        double sum_ratio = 0, min_r = 1e9, max_r = 0;
        for (const auto& g : results) {
            if (g.parallel.skipped || g.ips4o_par.skipped) continue;
            ++total;
            const double r = g.parallel.ms_best / g.ips4o_par.ms_best;
            sum_ratio += r; min_r = std::min(min_r, r); max_r = std::max(max_r, r);
            if (r < 1.0) ++wins;
        }
        std::cout << "[PARALLEL " << NUM_THREADS << " threads] "
                  << "DialSort-Par vs IPS4o-par\n"
                  << "  Configs     : " << total << "\n"
                  << "  DS-Par faster : " << wins << " / " << total << "\n"
                  << "  Avg ratio   : " << (total > 0 ? sum_ratio/total : 0)
                  << "  (< 1.0 = DS-Par faster)\n"
                  << "  Best ratio  : " << min_r << "\n"
                  << "  Worst ratio : " << max_r << "\n\n";
    }
#else
    std::cout << "[PARALLEL] IPS4o-par not compiled. "
              << "Recompile without -DSKIP_IPS4O_PARALLEL + link -ltbb\n\n";
#endif

    // Correctness
    bool all_ok = true;
    for (const auto& g : results) {
        if (!g.dialsort.correct || !g.parallel.correct || !g.ips4o_seq.correct)
        { all_ok = false; break; }
#ifndef SKIP_IPS4O_PARALLEL
        if (!g.ips4o_par.correct) { all_ok = false; break; }
#endif
    }
    std::cout << "All correctness checks: "
              << (all_ok ? "PASSED" : "*** FAILURES ***") << "\n\n";

    // ── CSV ───────────────────────────────────────────────────────────────────
    std::cout << "================================================================\n"
              << "CSV OUTPUT\n"
              << "================================================================\n"
              << "algo,dist,N,U,ms_best,ms_mean,ms_std,Mkeys_per_s,"
              << "speedup_vs_std,correct\n";
    for (const auto& g : results) {
        csv_row(g.std_sort);
        csv_row(g.dialsort);
        csv_row(g.parallel);
        csv_row(g.ips4o_seq);
#ifndef SKIP_IPS4O_PARALLEL
        csv_row(g.ips4o_par);
#endif
    }

    return 0;
}//
// Created by Windows on 18/04/2026.
//
