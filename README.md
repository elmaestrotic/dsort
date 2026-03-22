
# DialSort: Non-Comparative Integer Sorting via the Self-Indexing Principle

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.19169171.svg)](https://doi.org/10.5281/zenodo.19169171)

---

## Overview

DialSort is a non-comparative sorting model for bounded-universe integer keys that departs from the classical comparison-based paradigm. Instead of constructing order through pairwise comparisons, DialSort leverages a fundamental property of integers:

> Each key `k` is both a value and an address in an ordered space.

This enables sorting to be reformulated as a **geometric read operation over memory**, rather than a sequence of comparison decisions.

---

## Core Idea

For a bounded universe `k ∈ [0, U-1]`, each input key directly activates a position in a histogram:

```
H[k]++
```

After ingestion, `H[k]` contains the **complete ordered representation** of the dataset. No prefix sum is required. No reconstruction is mandatory.

The sorted sequence is obtained (if needed) via:

```
emit k exactly H[k] times, for k = 0 to U-1
```

> The order is not constructed. It is already encoded in the address space.

---

## Conceptual Pipeline (with CRN)

```
┌────────────────────┐
│     Input Lanes    │
│   k1  k2  k3  k4   │
└─────────┬──────────┘
          │
          ▼
┌─────────────────────────────────┐
│   Conflict Resolution Network   │
│              (CRN)              │
│                                 │
│   [k1==k2?] → merge/add         │
│   [k3==k4?] → merge/add         │
│          ↓                      │
│      reduction tree             │
└─────────┬───────────────────────┘
          │
          ▼
┌──────────────────────────────┐
│       Histogram H[k]         │
│     (canonical ordered       │
│          state)              │
└─────────┬────────────────────┘
          │
          ▼
┌──────────────────────────────┐
│       Geometric Scan         │
│        k = 0 ... U-1         │
└─────────┬────────────────────┘
          │
          ▼
┌──────────────────────────────┐
│  Output Vector  (optional)   │
└──────────────────────────────┘
```

### Key interpretation

- `H[k]` is **not an intermediate structure** — it is the **canonical ordered representation**
- The output vector is a **derived projection**, needed only for legacy array interfaces
- No order comparison (`<`, `>`, or `≤`) is performed at any stage

---

## Conflict Resolution Network (CRN)

The CRN resolves concurrent write conflicts without any order comparison:

```
if k_i == k_j  →  emit (k, c_i + c_j)   // additive merge
else            →  emit both pairs unchanged
```

Properties:

| Property | Value |
|---|---|
| Comparison type used | Equality only (`==`) |
| Semantic model | Additive reduction, not arbitration |
| Information loss | None — all counts preserved |
| Depth | ⌈log₂ k⌉ stages for k parallel lanes |
| Latency | Fixed, independent of n |

---

## Differentiation from Counting Sort

| Property | Counting Sort | DialSort |
|---|---|---|
| Prefix sum | Required | **Not required** |
| Output vector | Mandatory | **Optional** |
| Histogram role | Intermediate counting structure | **Canonical ordered state** |
| Parallel model | Locks / atomics | **Native CRN** |
| Ordering model | Constructed | **Revealed** |
| Conflict handling | None / serialized | **Additive CRN** |

> DialSort is not a variant of counting sort. It redefines the role of the histogram from an intermediate structure preceding reconstruction to the canonical representation of order itself.

---

## Implementations

This repository includes three variants:

| Variant | Description | Complexity |
|---|---|---|
| **DialSort-Counting** | Sequential histogram baseline | O(n + U) |
| **DialSort-Parallel** | Multi-thread with per-thread histograms + additive merge (software CRN analog) | O(n/p + p·U) |
| **DialSort-Radix** | LSD radix sort, base 256, 4 passes — handles full int32 range | O(4n) = O(n) |

---

## Experimental Results

All benchmarks were executed under the following conditions:

```
Compiler : g++ 13.1.0
Flags    : -O2 -std=c++17 -pthread
Hardware : Intel x86-64, 8 hardware threads
Timing   : std::chrono::high_resolution_clock
Warmup   : 3 discarded runs
Measured : best-of-7 runs
Seed     : 20260321
Verified : check_sorted() after every run
```

### Peak results — bounded universe

| Algorithm | Distribution | N | U | Speedup vs std::sort | Throughput |
|---|---|---|---|---|---|
| DialSort-Parallel | uniform | 10⁷ | 1,024 | **39.77×** ★ | 115.1 M keys/s |
| DialSort-Parallel | skewed | 10⁷ | 256 | 37.97× | **115.9 M keys/s** ★ |
| DialSort-Counting | uniform | 10⁷ | 1,024 | 30.26× | 87.6 M keys/s |

### Peak results — full int32 range

| Algorithm | N | Speedup vs std::sort | Throughput |
|---|---|---|---|
| DialSort-Radix | 10⁷ | **14.61×** | 38.2 M keys/s |

All 208 benchmark configurations (192 bounded-universe + 16 full-int32) passed correctness verification.

---

## Reproducibility

The complete experimental dataset is available at:

```
/results/results_full.csv
```

CSV columns:

```
algorithm, distribution, N, U, time_ms, throughput_mkeys_per_s,
speedup_vs_std_sort, correct, best_run_index
```

### Build and run

```bash
# Compile
g++ -O2 -std=c++17 -pthread -o bench dialsort_benchmark_v3.cpp

# Run
./bench
```

---

## Complexity

| Phase | Time | Space |
|---|---|---|
| Ingestion | O(n) | O(U) |
| CRN merge (parallel) | O(p·U) | O(p·U) |
| Projection (scan) | O(U + n) | O(1) additional |
| **Total** | **O(n + U)** | **O(U)** |

---

## Limitations

- Requires bounded universe `[0, U-1]` for the counting variant
- Performance degrades when `U ≫ n` (O(U) scan dominates)
- Practical guideline: use DialSort-Counting when `n ≳ 10·U`
- For arbitrary keys, use DialSort-Radix or comparison-based sorts

---

## Physical Interpretation

DialSort can be understood as a physical read system across three substrates:

| Substrate | Scan signal | Active state | Inactive state |
|---|---|---|---|
| **Fluidic** | Water jet | Open gate (flow) | Closed gate (no flow) |
| **Electrical (FPGA)** | Clock sweep | Register energized | Register silent |
| **Photonic** | Light pulse | Resonator in resonance | Resonator transparent |

In all cases, the physical state of the medium **is** the answer. No comparator is evaluated.

> DialSort does not traverse a sorted sequence. It illuminates one.

---

## Paper

This repository accompanies the paper:

**DialSort: Non-Comparative Integer Sorting via the Self-Indexing Principle**  
Alexander Narváez — Independent Researcher / Universidad EAFIT, Colombia

📄 [DOI: 10.5281/zenodo.19169171](https://doi.org/10.5281/zenodo.19169171)

---

## Author

**Alexander Narváez**  
Independent Researcher / Universidad EAFIT  
Envigado, Antioquia, Colombia  
anarvaez1@eafit.edu.co
