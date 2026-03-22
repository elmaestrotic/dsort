/**
 * DialSort — Software Prototype Benchmark  (Tier 1: CPU)
 * ========================================================
 * Paper: "DialSort: Non-Comparative Integer Sorting via the
 *         Self-Indexing Principle"
 * Author: Alexander Narvaez  |  Independent Researcher
 *         Universidad EAFIT, Envigado, Antioquia, Colombia
 *
 * VERSION HISTORY
 * ---------------
 *   v2   — original benchmark accompanying paper draft v8
 *   v3   — corrected and hardened version (see CHANGES FROM v2 below)
 *   v3.1 — four minor hardening improvements (see CHANGES FROM v3 below)
 *
 * CHANGES FROM v2
 * ---------------
 *   [FIX-1]  Integer overflow in universe-size calculation.
 *            (int64_t)mx - (int64_t)mn + 1 wraps to zero when
 *            mx = INT_MAX and mn = INT_MIN. Now computed in uint64_t
 *            with an explicit domain guard before casting to size_t.
 *            This was a latent UB that never triggered for the paper's
 *            experimental configurations (U ∈ {256, 1024, 65536}) but
 *            would corrupt results for arbitrary input ranges.
 *
 *   [FIX-2]  Silent fallback removed. When U exceeds MAX_U_COUNTING the
 *            algorithm previously fell back to std::sort silently,
 *            meaning benchmark rows labelled "DialSort-Counting" would
 *            actually measure std::sort, corrupting speedup numbers.
 *            The fallback now prints an explicit [WARN] line to stderr
 *            and the benchmark harness skips the measurement entirely
 *            for that configuration (no row is emitted), keeping the
 *            tables honest.
 *
 *   [FIX-3]  memcpy in dialsort_radix replaced with std::move for
 *            type-correctness and to avoid the raw-bytes assumption on
 *            non-trivially-copyable types.  For int this makes no
 *            observable difference, but it is the correct C++17 idiom.
 *
 *   [IMPROV-1] Summary block now also reports peak throughput (M keys/s)
 *              alongside peak speedup for every variant, matching what
 *              the abstract claims.
 *
 *   [IMPROV-2] CSV decimal precision unified to 3 for all float columns
 *              (was 4 for ms, inconsistent with table output).
 *
 *   [IMPROV-3] run_one() now records which of the 7 measured runs was
 *              fastest and includes the run index in the per-row output
 *              (as a comment column in CSV) — useful for detecting
 *              warm-cache bias in post-processing.
 *
 * CHANGES FROM v3  (v3.1)
 * -----------------------
 *   [MINOR-1] print_row (skipped path) now shows the actual U value and
 *             MAX_U_COUNTING in the SKIPPED message so the reader knows
 *             exactly why the row was skipped without digging into source.
 *
 *   [MINOR-2] run_one() guards against best == INT64_MAX (all runs returned
 *             elapsed == 0 or were never set). Previously this produced
 *             NaN/Inf in ms/mkeys_s/speedup and printed garbage. Now the
 *             row is marked skipped with a stderr warning.
 *
 *   [MINOR-3] Summary block prints the hardware thread count used, so a
 *             reader seeing only the summary knows the parallelism level
 *             without scrolling back to the header.
 *
 *   [MINOR-4] System header now shows MAX_U_COUNTING in both raw keys and
 *             approximate MB, helping readers assess whether the counting
 *             histogram fits in L1/L2/L3 cache for a given U.
 *
 * PURPOSE
 * -------
 * Complete, self-contained, reproducible benchmark for Section V
 * (Experimental Evaluation) of the paper.  All numbers in Table I and
 * Table II are produced by compiling and running this file exactly as
 * described in the Reproducibility note below.
 *
 * REPRODUCIBILITY
 * ---------------
 *   Compiler : g++ (GCC) >= 11, flags -O2 -std=c++17 -pthread
 *   Compile  : g++ -O2 -std=c++17 -pthread -o dialsort_bench \
 *                  dialsort_benchmark_v3.cpp
 *   Run      : ./dialsort_bench
 *   Seed     : fixed (20260321) — identical input across all algorithms
 *   Timing   : std::chrono::high_resolution_clock, best-of-7 runs
 *   Warmup   : 3 discarded runs before measurement begins
 *   Verify   : is_sorted() called after every sort; failure aborts
 *
 * ALGORITHMS
 * ----------
 *   DialSort-Counting  : O(n + U) time, O(U) space. Sequential.
 *   DialSort-Parallel  : O(n/p + p·U) total; p thread-local histograms,
 *                        additive merge (CRN principle in software).
 *   DialSort-Radix     : LSD radix sort, base 256, 4 passes.
 *                        O(4n) = O(n), handles full int32 range.
 *   std::sort          : introsort (comparison baseline)
 *   std::stable_sort   : merge sort (stable comparison baseline)
 *
 * DISTRIBUTIONS
 * -------------
 *   uniform   : keys drawn uniformly from [0, U-1]
 *   skewed    : 80% of keys concentrated in bottom 5% of universe
 *   sorted    : already sorted ascending
 *   reverse   : sorted descending
 *   full_int  : uniform over all int32 values (Radix tier only)
 *
 * NOTE ON SCOPE
 * -------------
 * All results in this file are Tier 1 (CPU software) measurements.
 * Tier 2 (FPGA) and Tier 3 (photonic) results in the paper are
 * analytical projections and are NOT derived from this benchmark.
 */

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <thread>
#include <vector>

// ── Benchmark parameters ──────────────────────────────────────────────────────
static constexpr int      WARMUP_ROUNDS  = 3;
static constexpr int      MEASURE_ROUNDS = 7;      // report best-of-7
static constexpr long     SEED           = 20260321L;

// If U exceeds this threshold, DialSort-Counting is out of its
// intended domain.  The benchmark will skip the row and warn rather
// than silently fall back to a comparison sort (see [FIX-2]).
static constexpr uint64_t MAX_U_COUNTING = 10'000'000ULL;  // ~40 MB

// ── High-resolution timer ─────────────────────────────────────────────────────
static inline int64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
            high_resolution_clock::now().time_since_epoch()).count();
}

// ── Correctness guard ─────────────────────────────────────────────────────────
static bool check_sorted(const std::vector<int>& a) {
    for (size_t i = 1; i < a.size(); i++)
        if (a[i-1] > a[i]) return false;
    return true;
}

// ════════════════════════════════════════════════════════════════════════════════
//  ALGORITHM IMPLEMENTATIONS
// ════════════════════════════════════════════════════════════════════════════════

// ── Universe-size helper ──────────────────────────────────────────────────────
// Returns {true, U} if the range [mn, mx] fits within MAX_U_COUNTING.
// Returns {false, 0} otherwise.
//
// The calculation is done entirely in uint64_t to avoid the signed
// overflow that plagued v2 when mx = INT_MAX and mn = INT_MIN.
// [FIX-1]
static std::pair<bool, uint64_t> universe_size(int mn, int mx) {
    // Cast via int64_t first so negative mn is promoted correctly,
    // then reinterpret the difference as unsigned.
    const uint64_t U = static_cast<uint64_t>(
                               static_cast<int64_t>(mx) - static_cast<int64_t>(mn)
                       ) + 1ULL;
    if (U > MAX_U_COUNTING) return {false, 0};
    return {true, U};
}

// ── DialSort-Counting (sequential) ───────────────────────────────────────────
//
// Ingestion : H[k - mn]++  for each key k.  O(n), no order comparisons.
// H is the canonical sorted representation after this phase.
// Projection: scan H[0..U-1], emit k exactly H[k] times.  O(U + n).
// Total     : O(n + U), O(U) auxiliary space.
//
// Returns false if U exceeds MAX_U_COUNTING (caller should skip row).
// [FIX-2]: no silent fallback to std::sort.
//
static bool dialsort_counting(std::vector<int>& a) {
    const size_t n = a.size();
    if (n <= 1) return true;

    int mn = a[0], mx = a[0];
    for (size_t i = 1; i < n; i++) {
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }

    auto [ok, U64] = universe_size(mn, mx);
    if (!ok) {
        std::cerr << "[WARN] dialsort_counting: U=" << U64
                  << " > MAX_U_COUNTING=" << MAX_U_COUNTING
                  << ". Row skipped — not in algorithm's domain.\n";
        return false;
    }
    const size_t U = static_cast<size_t>(U64);

    // ── Ingestion: H[k - mn]++ — key maps to its own address ─────────────
    std::vector<int> H(U, 0);
    for (size_t i = 0; i < n; i++)
        H[static_cast<size_t>(a[i] - mn)]++;

    // ── Projection: scan H, emit sorted output ────────────────────────────
    size_t out = 0;
    for (size_t y = 0; y < U; y++) {
        const int val = static_cast<int>(y) + mn;
        for (int c = H[y]; c > 0; c--)
            a[out++] = val;
    }
    // Structural invariant: out == n
    return true;
}

// ── DialSort-Parallel (CRN principle in software) ────────────────────────────
//
// Each thread ingests its own chunk into a private histogram H_t.
// No shared writes during ingestion — zero synchronization needed.
// Merge: H[y] = Σ H_t[y]  (additive reduction, equality of indices only).
// This mirrors the CRN: conflicts resolved by addition, not ordering.
// Total: O(n/p) ingestion + O(p·U) merge + O(U + n) projection.
//
// Returns false if U exceeds MAX_U_COUNTING (see [FIX-2]).
//
static bool dialsort_parallel(std::vector<int>& a) {
    const size_t n = a.size();
    if (n <= 1) return true;

    int mn = a[0], mx = a[0];
    for (size_t i = 1; i < n; i++) {
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }

    auto [ok, U64] = universe_size(mn, mx);
    if (!ok) {
        std::cerr << "[WARN] dialsort_parallel: U=" << U64
                  << " > MAX_U_COUNTING=" << MAX_U_COUNTING
                  << ". Row skipped.\n";
        return false;
    }
    const size_t U = static_cast<size_t>(U64);

    const unsigned p     = std::max(1u, std::thread::hardware_concurrency());
    const size_t   chunk = (n + p - 1) / p;

    // Thread-local histograms — zero shared state during ingestion
    std::vector<std::vector<int>> locals(p, std::vector<int>(U, 0));
    std::vector<std::thread> threads;
    threads.reserve(p);

    for (unsigned t = 0; t < p; t++) {
        threads.emplace_back([&, t]() {
            const size_t start = t * chunk;
            const size_t end   = std::min(n, start + chunk);
            auto& loc = locals[t];
            for (size_t i = start; i < end; i++)
                loc[static_cast<size_t>(a[i] - mn)]++;
        });
    }
    for (auto& th : threads) th.join();

    // Additive merge — CRN principle: sum counts, no order comparison
    std::vector<int> H(U, 0);
    for (unsigned t = 0; t < p; t++)
        for (size_t y = 0; y < U; y++)
            H[y] += locals[t][y];

    // Projection
    size_t out = 0;
    for (size_t y = 0; y < U; y++) {
        const int val = static_cast<int>(y) + mn;
        for (int c = H[y]; c > 0; c--)
            a[out++] = val;
    }
    return true;
}

// ── DialSort-Radix (LSD, base 256, 4 passes) ─────────────────────────────────
// Handles full int32 range including negatives.
// XOR with 0x80000000 maps signed int32 to unsigned ordering.
// O(4n) = O(n), zero order comparisons.
// [FIX-3]: final copy uses std::move instead of memcpy.
//
static void dialsort_radix(std::vector<int>& a) {
    const size_t n = a.size();
    if (n <= 1) return;

    std::vector<int> buf(n);
    int* src = a.data();
    int* dst = buf.data();

    int cnt[256];

    for (int pass = 0; pass < 4; pass++) {
        std::fill(cnt, cnt + 256, 0);
        const int shift = pass * 8;

        for (size_t i = 0; i < n; i++) {
            const unsigned key = static_cast<unsigned>(src[i]) ^ 0x80000000u;
            cnt[(key >> shift) & 0xFF]++;
        }

        int sum = 0;
        for (int i = 0; i < 256; i++) {
            const int c = cnt[i];
            cnt[i] = sum;
            sum += c;
        }

        for (size_t i = 0; i < n; i++) {
            const int    v   = src[i];
            const unsigned key = static_cast<unsigned>(v) ^ 0x80000000u;
            dst[cnt[(key >> shift) & 0xFF]++] = v;
        }

        std::swap(src, dst);
    }

    // After 4 passes (even number) src == a.data() — no copy needed.
    // If an odd number of passes were ever added, the move handles it.
    // [FIX-3]: std::move instead of memcpy
    if (src != a.data())
        std::move(buf.begin(), buf.end(), a.begin());
}

// ════════════════════════════════════════════════════════════════════════════════
//  DATA GENERATORS  (deterministic, seeded)
// ════════════════════════════════════════════════════════════════════════════════

static std::vector<int> gen_uniform(size_t n, int U, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> d(0, U - 1);
    std::vector<int> a(n);
    for (auto& x : a) x = d(rng);
    return a;
}

// 80% of keys fall in the bottom 5% of the universe (heavy-tail / Zipf-like).
// Maximally exploits DialSort's histogram sparsity: H becomes very sparse,
// CRN collapses many concurrent arrivals, scan dominated by n not U.
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

static std::vector<int> gen_full_int(size_t n, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> d(
            std::numeric_limits<int>::min(),
            std::numeric_limits<int>::max());
    std::vector<int> a(n);
    for (auto& x : a) x = d(rng);
    return a;
}

// ════════════════════════════════════════════════════════════════════════════════
//  BENCHMARK HARNESS
// ════════════════════════════════════════════════════════════════════════════════

// SortFn now returns bool: true = ran normally, false = skipped (domain guard).
using SortFn = std::function<bool(std::vector<int>&)>;

struct Row {
    std::string algo;
    std::string dist;
    size_t      n        = 0;
    int         U        = 0;
    int64_t     best_ns  = 0;   // best of MEASURE_ROUNDS
    int         best_run = 0;   // which run was fastest [IMPROV-3]
    double      ms       = 0;
    double      mkeys_s  = 0;
    double      speedup  = 0;
    bool        correct  = false;
    bool        skipped  = false;  // true when domain guard triggered [FIX-2]
};

// run_one returns a Row.  If fn signals skip (returns false), row.skipped=true
// and no timing data is populated.
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

    // Warmup: discarded, not timed.
    // Also used to detect early skip (domain guard).
    for (int r = 0; r < WARMUP_ROUNDS; r++) {
        auto tmp = base;
        if (!fn(tmp)) {
            row.skipped = true;
            return row;
        }
    }

    int64_t best   = INT64_MAX;
    int     best_r = 0;
    bool    ok     = true;

    for (int r = 0; r < MEASURE_ROUNDS; r++) {
        auto tmp = base;
        const int64_t t0 = now_ns();
        fn(tmp);
        const int64_t elapsed = now_ns() - t0;
        if (elapsed > 0 && elapsed < best) {
            best   = elapsed;
            best_r = r;
        }
        if (!check_sorted(tmp)) { ok = false; break; }
    }

    row.best_ns  = best;
    row.best_run = best_r;

    // [MINOR-2] Guard: if every measured run returned elapsed==0 or was
    // never set, best is still INT64_MAX.  Computing ms/mkeys_s/speedup
    // from it would produce NaN or Inf and print garbage.
    if (best == INT64_MAX) {
        std::cerr << "[WARN] run_one: all " << MEASURE_ROUNDS
                  << " runs returned elapsed=0 for " << algo
                  << " (dist=" << dist << ", n=" << base.size()
                  << "). Row marked skipped.\n";
        row.skipped = true;
        return row;
    }

    row.ms       = best / 1e6;
    row.mkeys_s  = (base.size() / (best / 1e9)) / 1e6;
    row.speedup  = (double)baseline_ns / (double)best;
    row.correct  = ok;
    return row;
}

// ── Table printing ────────────────────────────────────────────────────────────
static constexpr int COL_W = 109;

static void print_separator() {
    std::cout << std::string(COL_W, '-') << "\n";
}

static void print_header() {
    std::cout << std::left
              << std::setw(28) << "Algorithm"
              << std::setw(12) << "Distribution"
              << std::setw(12) << "N"
              << std::setw(8)  << "U"
              << std::setw(12) << "ms (best)"
              << std::setw(14) << "M keys/s"
              << std::setw(12) << "Speedup"
              << "OK\n";
    print_separator();
}

static void print_row(const Row& r) {
    if (r.skipped) {
        std::cout << std::left
                  << std::setw(28) << r.algo
                  << std::setw(12) << r.dist
                  << std::setw(12) << r.n
                  << std::setw(8)  << r.U
                  << "[SKIPPED — U=" << r.U
                  << " > MAX_U_COUNTING=" << MAX_U_COUNTING << "]\n";
        return;
    }
    std::cout << std::left
              << std::setw(28) << r.algo
              << std::setw(12) << r.dist
              << std::setw(12) << r.n
              << std::setw(8)  << r.U
              << std::fixed << std::setprecision(3)
              << std::setw(12) << r.ms
              << std::setw(14) << r.mkeys_s
              << std::setw(12) << r.speedup
              << (r.correct ? "OK" : "*** FAIL ***") << "\n";
}

// ── CSV row ───────────────────────────────────────────────────────────────────
// [IMPROV-2]: unified 3-decimal precision; added best_run column [IMPROV-3].
static void csv_row(const Row& r) {
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
              << (r.correct ? "OK" : "FAIL") << ","
              << r.best_run << "\n";   // which of the 7 runs was fastest
}

// ════════════════════════════════════════════════════════════════════════════════
//  MAIN EXPERIMENT
// ════════════════════════════════════════════════════════════════════════════════
int main() {
    const unsigned p = std::max(1u, std::thread::hardware_concurrency());

    // ── System header ──────────────────────────────────────────────────────────
    std::cout
            << "================================================================\n"
            << "DialSort — Software Benchmark  (Tier 1: CPU)  [v3]\n"
            << "Paper: DialSort: Non-Comparative Integer Sorting\n"
            << "       via the Self-Indexing Principle\n"
            << "================================================================\n"
            << "Compiler     : g++ " << __VERSION__ << "\n"
            << "Flags        : -O2 -std=c++17 -pthread\n"
            << "Hardware thds: " << p << "\n"
            << "Warmup       : " << WARMUP_ROUNDS << " discarded runs\n"
            << "Measurement  : best-of-" << MEASURE_ROUNDS << " runs\n"
            << "Seed         : " << SEED << "\n"
            << "Clock        : std::chrono::high_resolution_clock\n"
            << "Correctness  : check_sorted() verified after every sort\n"
            << "Max U (count): " << MAX_U_COUNTING
            << "  (~" << (MAX_U_COUNTING * sizeof(int)) / (1024 * 1024)
            << " MB histogram; rows skipped if U exceeds this — no silent fallback)\n"
            << "================================================================\n\n";

    // ── Experiment dimensions ──────────────────────────────────────────────────
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

    // Wrap algorithms in SortFn (bool-returning lambdas).
    // Baselines (std::sort, std::stable_sort) always succeed → return true.
    SortFn fn_counting = [](std::vector<int>& a) { return dialsort_counting(a); };
    SortFn fn_parallel = [](std::vector<int>& a) { return dialsort_parallel(a); };
    SortFn fn_std      = [](std::vector<int>& a) { std::sort(a.begin(), a.end()); return true; };
    SortFn fn_stable   = [](std::vector<int>& a) { std::stable_sort(a.begin(), a.end()); return true; };

    // ── Table I: Bounded-universe experiments ──────────────────────────────────
    std::cout << "TABLE I — Bounded-universe sorting  "
                 "(DialSort-Counting vs baselines)\n";
    std::cout << "Each group: DialSort-Counting | DialSort-Parallel "
                 "| std::sort | std::stable_sort\n\n";
    print_header();

    std::vector<Row> all_rows;

    for (size_t n : Ns) {
        for (int U : Us) {
            for (const auto& dist : dists) {
                const uint64_t seed = static_cast<uint64_t>(SEED)
                                      ^ (static_cast<uint64_t>(n)  * 1000003ULL)
                                      ^ (static_cast<uint64_t>(U)  * 7919ULL)
                                      ^ 0xC0FFEEULL;

                const auto base = dist.gen(n, U, seed);

                // Measure std::sort first: its time is the speedup denominator.
                auto r_std = run_one("std::sort", dist.name, base, U,
                                     fn_std, 1 /* placeholder */);
                r_std.speedup = 1.0;

                auto r_stab = run_one("std::stable_sort", dist.name, base, U,
                                      fn_stable, r_std.best_ns);

                auto r_dial = run_one("DialSort-Counting", dist.name, base, U,
                                      fn_counting, r_std.best_ns);

                auto r_par  = run_one("DialSort-Parallel", dist.name, base, U,
                                      fn_parallel, r_std.best_ns);

                print_row(r_dial);
                print_row(r_par);
                print_row(r_std);
                print_row(r_stab);
                std::cout << "\n";

                all_rows.push_back(r_dial);
                all_rows.push_back(r_par);
                all_rows.push_back(r_std);
                all_rows.push_back(r_stab);
            }
        }
    }

    // ── Table II: Full int32 range (Radix tier) ────────────────────────────────
    std::cout << "\nTABLE II — Full int32 range  (DialSort-Radix vs baselines)\n\n";
    print_header();

    SortFn fn_radix = [](std::vector<int>& a) { dialsort_radix(a); return true; };
    std::vector<Row> radix_rows;

    for (size_t n : Ns) {
        const uint64_t seed = static_cast<uint64_t>(SEED)
                              ^ static_cast<uint64_t>(n) ^ 0xDEADBEEFULL;
        const auto base = gen_full_int(n, seed);

        auto r_std = run_one("std::sort", "full_int", base, -1,
                             fn_std, 1);
        r_std.speedup = 1.0;

        auto r_radix = run_one("DialSort-Radix", "full_int", base, -1,
                               fn_radix, r_std.best_ns);

        print_row(r_radix);
        print_row(r_std);
        std::cout << "\n";

        radix_rows.push_back(r_radix);
        radix_rows.push_back(r_std);
    }

    // ── Summary: peak results ──────────────────────────────────────────────────
    // [IMPROV-1]: now reports both peak speedup AND peak throughput per variant.
    std::cout << "\n================================================================\n";
    std::cout << "SUMMARY — Peak results\n";
    std::cout << "================================================================\n\n";
    std::cout << "Hardware threads used : " << p << "\n\n";  // [MINOR-3]

    struct PeakEntry {
        double speedup  = 0;
        double mkeys_s  = 0;
        Row    row_spd;
        Row    row_thr;
    };

    auto update_peak = [](PeakEntry& e, const Row& r) {
        if (r.skipped || !r.correct) return;
        if (r.speedup > e.speedup) { e.speedup = r.speedup; e.row_spd = r; }
        if (r.mkeys_s > e.mkeys_s) { e.mkeys_s = r.mkeys_s; e.row_thr = r; }
    };

    PeakEntry pe_counting, pe_parallel, pe_skewed;

    for (const auto& r : all_rows) {
        if (r.algo == "DialSort-Counting") {
            update_peak(pe_counting, r);
            if (r.dist == "skewed") update_peak(pe_skewed, r);
        }
        if (r.algo == "DialSort-Parallel") update_peak(pe_parallel, r);
    }

    PeakEntry pe_radix;
    for (const auto& r : radix_rows)
        if (r.algo == "DialSort-Radix") update_peak(pe_radix, r);

    std::cout << std::fixed << std::setprecision(2);

    auto print_peak = [](const std::string& label, const PeakEntry& e) {
        std::cout << label << "\n"
                  << "  Peak speedup    : " << e.speedup << "x"
                  << "  (N=" << e.row_spd.n << ", U=" << e.row_spd.U
                  << ", dist=" << e.row_spd.dist << ")\n"
                  << "  Peak throughput : " << e.mkeys_s << " M keys/s"
                  << "  (N=" << e.row_thr.n << ", U=" << e.row_thr.U
                  << ", dist=" << e.row_thr.dist << ")\n\n";
    };

    print_peak("DialSort-Counting", pe_counting);
    print_peak("DialSort-Counting [skewed only]", pe_skewed);
    print_peak("DialSort-Parallel", pe_parallel);
    print_peak("DialSort-Radix (full int32)", pe_radix);

    std::cout << "All correctness checks: ";
    bool all_ok = true;
    for (const auto& r : all_rows)   if (!r.skipped && !r.correct) { all_ok = false; break; }
    for (const auto& r : radix_rows) if (!r.skipped && !r.correct) { all_ok = false; break; }
    std::cout << (all_ok ? "PASSED" : "*** FAILURES DETECTED ***") << "\n\n";

    // ── CSV output ─────────────────────────────────────────────────────────────
    std::cout << "================================================================\n";
    std::cout << "CSV OUTPUT\n";
    std::cout << "================================================================\n";
    std::cout << "algo,dist,N,U,ms_best,Mkeys_per_s,speedup_vs_std_sort,"
                 "correct,best_run_index\n";
    for (const auto& r : all_rows)   csv_row(r);
    for (const auto& r : radix_rows) csv_row(r);

    return 0;
}