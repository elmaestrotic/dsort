/**
 * DialSort vs. Classic Counting Sort — Direct Comparison Benchmark
 * =================================================================
 * Paper: "DialSort: Non-Comparative Integer Sorting via the
 *         Self-Indexing Principle"
 * Author: Alexander Narvaez  |  Independent Researcher
 *         Universidad EAFIT, Envigado, Antioquia, Colombia
 *
 * PURPOSE
 * -------
 * Side-by-side benchmark isolating DialSort-Counting against Classic
 * Counting Sort (textbook CLRS implementation with mandatory prefix-sum
 * and output-array reconstruction) across all experimental dimensions.
 *
 * This benchmark was designed to respond directly to reviewer comments
 * requesting empirical differentiation between DialSort and counting sort.
 *
 * WHAT IS MEASURED
 * ----------------
 * Three algorithms:
 *
 *   1. DialSort-Counting
 *      - Ingestion  : H[k - mn]++       (self-indexing, no prefix sum)
 *      - Projection : scan H, emit in place   (writes back to input array)
 *      - Passes     : 2   (ingestion + projection)
 *      - Output     : in-place (overwrites input)
 *      - Prefix sum : NONE
 *
 *   2. Classic Counting Sort (CLRS §8.2)
 *      - Count      : C[k - mn]++
 *      - Prefix sum : C[i] += C[i-1]     (mandatory reconstruction step)
 *      - Scatter    : B[C[k]--] = k      (stable placement into output array)
 *      - Passes     : 3   (count + prefix-sum + scatter)
 *      - Output     : separate output array B (then copied back)
 *      - Prefix sum : MANDATORY
 *
 *   3. std::sort (introsort baseline)
 *
 * KEY DIFFERENCES CAPTURED
 * ------------------------
 *   | Property              | DialSort-Counting    | Classic Counting Sort |
 *   |-----------------------|----------------------|-----------------------|
 *   | Prefix-sum pass       | None                 | Mandatory             |
 *   | Output array          | In-place             | Separate (O(n) extra) |
 *   | Total passes          | 2                    | 3                     |
 *   | Memory allocation     | O(U)                 | O(U + n)              |
 *   | Stability             | Not stable (counts)  | Stable (CLRS)         |
 *   | Histogram role        | Canonical repr.      | Intermediate struct   |
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
 *   Compile : g++ -O2 -std=c++17 -o bench_vs dialsort_vs_counting_bench.cpp
 *   Run     : ./bench_vs
 *   Seed    : fixed (20260321)
 *   Timing  : best-of-7 runs, 3 warmup discarded
 */

#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>




// ── Parameters ────────────────────────────────────────────────────────────────
static constexpr int      WARMUP_ROUNDS  = 3;
static constexpr int      MEASURE_ROUNDS = 7;
static constexpr long     SEED           = 20260321L;
static constexpr uint64_t MAX_U          = 10'000'000ULL;

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

// ── Universe-size guard (uint64_t to avoid signed overflow) ───────────────────
static std::pair<bool, uint64_t> universe_size(int mn, int mx) {
    const uint64_t U = static_cast<uint64_t>(
                               static_cast<int64_t>(mx) - static_cast<int64_t>(mn)
                       ) + 1ULL;
    return {U <= MAX_U, U};
}

// ════════════════════════════════════════════════════════════════════════════════
//  ALGORITHM 1 — DialSort-Counting
//  Passes: 2  (ingestion + projection)
//  Extra memory: O(U)  (histogram only)
//  Prefix sum: NONE
// ════════════════════════════════════════════════════════════════════════════════
static bool dialsort_counting(std::vector<int>& a) {
    const size_t n = a.size();
    if (n <= 1) return true;

    int mn = a[0], mx = a[0];
    for (size_t i = 1; i < n; ++i) {
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }

    auto [ok, U64] = universe_size(mn, mx);
    if (!ok) {
        std::cerr << "[WARN] dialsort_counting: U > MAX_U. Skipped.\n";
        return false;
    }
    const size_t U = static_cast<size_t>(U64);

    // Pass 1 — Ingestion: H[k - mn]++  (self-indexing, no comparison)
    std::vector<int> H(U, 0);
    for (size_t i = 0; i < n; ++i)
        H[static_cast<size_t>(a[i] - mn)]++;

    // Pass 2 — Projection: scan H, emit sorted output in-place
    // H is the canonical ordered representation; no prefix sum needed.
    size_t out = 0;
    for (size_t y = 0; y < U; ++y) {
        const int val = static_cast<int>(y) + mn;
        for (int c = H[y]; c > 0; --c)
            a[out++] = val;
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════════════════
//  ALGORITHM 2 — Classic Counting Sort  (CLRS §8.2, textbook)
//  Passes: 3  (count + prefix-sum + scatter)
//  Extra memory: O(U + n)  (count array + output array)
//  Prefix sum: MANDATORY
// ════════════════════════════════════════════════════════════════════════════════
static bool classic_counting_sort(std::vector<int>& a) {
    const size_t n = a.size();
    if (n <= 1) return true;

    int mn = a[0], mx = a[0];
    for (size_t i = 1; i < n; ++i) {
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }

    auto [ok, U64] = universe_size(mn, mx);
    if (!ok) {
        std::cerr << "[WARN] classic_counting_sort: U > MAX_U. Skipped.\n";
        return false;
    }
    const size_t U = static_cast<size_t>(U64);

    // Pass 1 — Count: C[k - mn]++
    std::vector<int> C(U, 0);
    for (size_t i = 0; i < n; ++i)
        C[static_cast<size_t>(a[i] - mn)]++;

    // Pass 2 — Prefix sum (MANDATORY in classic counting sort):
    // C[i] now holds the number of elements <= i + mn.
    // This step converts the count histogram into output positions.
    // DialSort does NOT perform this step.
    for (size_t i = 1; i < U; ++i)
        C[i] += C[i-1];

    // Pass 3 — Scatter: stable placement into output array B.
    // Requires O(n) extra memory for B, then copy back to a.
    std::vector<int> B(n);
    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
        const size_t idx = static_cast<size_t>(a[i] - mn);
        B[--C[idx]] = a[i];
    }

    // Copy B back to a (DialSort avoids this: it writes in-place)
    std::copy(B.begin(), B.end(), a.begin());
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
    std::bernoulli_distribution        pick_hot(0.80);
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
    double      speedup = 0;   // vs std::sort
    double      ratio   = 0;   // DialSort ms / Classic ms (< 1 = DialSort faster)
    bool        correct = false;
    bool        skipped = false;
};

struct GroupResult {
    Row    dial, classic, std_sort;
    double ratio = 0;
};

static Row run_one(const std::string& algo,
                   const std::string& dist,
                   const std::vector<int>& base,
                   int U,
                   SortFn fn,
                   int64_t baseline_ns)
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

    int64_t best = INT64_MAX;
    bool    ok   = true;

    for (int r = 0; r < MEASURE_ROUNDS; ++r) {
        auto tmp = base;
        const int64_t t0      = now_ns();
        fn(tmp);
        const int64_t elapsed = now_ns() - t0;
        if (elapsed > 0 && elapsed < best) best = elapsed;
        if (!check_sorted(tmp)) { ok = false; break; }
    }

    if (best == INT64_MAX) { row.skipped = true; return row; }

    row.ms      = best / 1e6;
    row.mkeys_s = (base.size() / (best / 1e9)) / 1e6;
    row.speedup = static_cast<double>(baseline_ns) / static_cast<double>(best);
    row.correct = ok;
    return row;
}

// ── Printing ──────────────────────────────────────────────────────────────────
static constexpr int COL_W = 120;

static void separator() { std::cout << std::string(COL_W, '-') << "\n"; }

static void print_table_header() {
    std::cout << std::left
              << std::setw(26) << "Algorithm"
              << std::setw(10) << "Dist"
              << std::setw(12) << "N"
              << std::setw(8)  << "U"
              << std::setw(12) << "ms (best)"
              << std::setw(14) << "M keys/s"
              << std::setw(12) << "vs std::sort"
              << std::setw(14) << "Ratio D/C"
              << "OK\n";
    separator();
}

static void print_row(const Row& r, double ratio) {
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
              << std::setw(12) << r.ms
              << std::setw(14) << r.mkeys_s
              << std::setw(12) << r.speedup;
    if (ratio > 0)
        std::cout << std::setw(14) << ratio;
    else
        std::cout << std::setw(14) << "—";
    std::cout << (r.correct ? "OK" : "*** FAIL ***") << "\n";
}

static void csv_row(const Row& r, double ratio) {
    if (r.skipped) {
        std::cout << r.algo << "," << r.dist << "," << r.n << "," << r.U
                  << ",SKIPPED,SKIPPED,SKIPPED,SKIPPED,SKIPPED\n";
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
              << ratio     << ","
              << (r.correct ? "OK" : "FAIL") << "\n";
}

// ════════════════════════════════════════════════════════════════════════════════
//  MAIN
// ════════════════════════════════════════════════════════════════════════════════
int main() {
    std::cout
            << "================================================================\n"
            << "DialSort vs. Classic Counting Sort — Direct Comparison Benchmark\n"
            << "Paper: DialSort: Non-Comparative Integer Sorting\n"
            << "       via the Self-Indexing Principle\n"
            << "================================================================\n"
            << "Compiler     : g++ " << __VERSION__ << "\n"
            << "Flags        : -O2 -std=c++17\n"
            << "Warmup       : " << WARMUP_ROUNDS << " discarded runs\n"
            << "Measurement  : best-of-" << MEASURE_ROUNDS << " runs\n"
            << "Seed         : " << SEED << "\n"
            << "Clock        : std::chrono::high_resolution_clock\n"
            << "Correctness  : check_sorted() after every run\n"
            << "\n"
            << "ALGORITHM STRUCTURAL DIFFERENCES\n"
            << "  DialSort-Counting  : 2 passes, O(U) memory,     NO prefix sum, in-place output\n"
            << "  Classic Count Sort : 3 passes, O(U+n) memory,   MANDATORY prefix sum, output array\n"
            << "  std::sort          : O(n log n), comparison-based baseline\n"
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

    SortFn fn_dial    = [](std::vector<int>& a){ return dialsort_counting(a); };
    SortFn fn_classic = [](std::vector<int>& a){ return classic_counting_sort(a); };
    SortFn fn_std     = [](std::vector<int>& a){ std::sort(a.begin(), a.end()); return true; };

    std::cout << "TABLE — DialSort-Counting vs Classic Counting Sort vs std::sort\n";
    std::cout << "Column 'Ratio D/C': DialSort ms / Classic ms  "
                 "(< 1.0 means DialSort is faster)\n\n";
    print_table_header();

    // Accumulate for summary
    std::vector<GroupResult> results;

    for (size_t n : Ns) {
        for (int U : Us) {
            for (const auto& dist : dists) {
                const uint64_t seed = static_cast<uint64_t>(SEED)
                                      ^ (static_cast<uint64_t>(n) * 1000003ULL)
                                      ^ (static_cast<uint64_t>(U) * 7919ULL)
                                      ^ 0xC0FFEEULL;

                const auto base = dist.gen(n, U, seed);

                // Measure std::sort first (speedup denominator)
                auto r_std = run_one("std::sort", dist.name, base, U, fn_std, 1);
                r_std.speedup = 1.0;

                // Measure both non-comparative algorithms
                auto r_dial    = run_one("DialSort-Counting",   dist.name, base, U,
                                         fn_dial,    r_std.ms > 0 ? (int64_t)(r_std.ms * 1e6) : 1);
                auto r_classic = run_one("Classic-CountSort",   dist.name, base, U,
                                         fn_classic, r_std.ms > 0 ? (int64_t)(r_std.ms * 1e6) : 1);

                // Recalculate speedup using std::sort best_ns
                int64_t std_ns = static_cast<int64_t>(r_std.ms * 1e6);
                if (!r_dial.skipped && std_ns > 0)
                    r_dial.speedup    = r_std.ms / r_dial.ms;
                if (!r_classic.skipped && std_ns > 0)
                    r_classic.speedup = r_std.ms / r_classic.ms;

                // Ratio: DialSort ms / Classic ms
                double ratio = 0.0;
                if (!r_dial.skipped && !r_classic.skipped && r_classic.ms > 0)
                    ratio = r_dial.ms / r_classic.ms;

                print_row(r_dial,    ratio);
                print_row(r_classic, 0);
                print_row(r_std,     0);
                std::cout << "\n";

                GroupResult g;
                g.dial     = r_dial;
                g.classic  = r_classic;
                g.std_sort = r_std;
                g.ratio    = ratio;
                results.push_back(g);
            }
        }
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    std::cout << "================================================================\n";
    std::cout << "SUMMARY — DialSort-Counting vs Classic Counting Sort\n";
    std::cout << "================================================================\n\n";

    double min_ratio = 1e9, max_ratio = 0, sum_ratio = 0;
    int    count = 0, dial_faster = 0, classic_faster = 0, tie = 0;

    for (const auto& g : results) {
        if (g.dial.skipped || g.classic.skipped) continue;
        min_ratio = std::min(min_ratio, g.ratio);
        max_ratio = std::max(max_ratio, g.ratio);
        sum_ratio += g.ratio;
        ++count;
        if (g.ratio < 0.99)       ++dial_faster;
        else if (g.ratio > 1.01)  ++classic_faster;
        else                      ++tie;
    }

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Configurations measured       : " << count << "\n"
              << "DialSort faster (ratio < 0.99): " << dial_faster    << " / " << count << "\n"
              << "Classic faster  (ratio > 1.01): " << classic_faster << " / " << count << "\n"
              << "Effectively tied (ratio ≈ 1.0) : " << tie           << " / " << count << "\n"
              << "Min ratio  D/C (best DialSort) : " << min_ratio     << "x\n"
              << "Max ratio  D/C (worst DialSort): " << max_ratio     << "x\n"
              << "Avg ratio  D/C                 : " << (count > 0 ? sum_ratio / count : 0) << "x\n\n";

    std::cout << "Structural cost accounting:\n"
              << "  DialSort:      2 passes, O(U) alloc, NO prefix-sum, writes in-place\n"
              << "  Classic CS:    3 passes, O(U+n) alloc, mandatory prefix-sum + scatter + copy\n"
              << "  Ratio < 1.0 => DialSort's pass and memory savings are empirically measurable\n\n";

    // ── Correctness summary ────────────────────────────────────────────────────
    bool all_ok = true;
    for (const auto& g : results) {
        if (!g.dial.skipped    && !g.dial.correct)    { all_ok = false; break; }
        if (!g.classic.skipped && !g.classic.correct) { all_ok = false; break; }
    }
    std::cout << "All correctness checks: " << (all_ok ? "PASSED" : "*** FAILURES ***") << "\n\n";

    // ── CSV ───────────────────────────────────────────────────────────────────
    std::cout << "================================================================\n";
    std::cout << "CSV OUTPUT\n";
    std::cout << "================================================================\n";
    std::cout << "algo,dist,N,U,ms_best,Mkeys_per_s,speedup_vs_std,ratio_dial_over_classic,correct\n";
    for (const auto& g : results) {
        csv_row(g.dial,    g.ratio);
        csv_row(g.classic, 0);
        csv_row(g.std_sort, 0);
    }

    return 0;
}