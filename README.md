# DialSort: Non-Comparative Integer Sorting via the Self-Indexing Principle [![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.19169171.svg)](https://doi.org/10.5281/zenodo.19169171)

## Overview

DialSort is a non-comparative sorting model for integer keys that departs from the classical comparison-based paradigm. Instead of constructing order through pairwise comparisons, DialSort leverages a fundamental property of integers:

> Each key `k` is both a value and an address in an ordered space.

This enables sorting to be reformulated as a **geometric read operation over memory**, rather than a sequence of comparison decisions.

---

## Core Idea

For a bounded universe `k ∈ [0, U-1]`, each input key directly activates a position in a histogram:


H[k]++


After ingestion, the histogram `H[k]` contains the **complete ordered representation** of the dataset.

The sorted sequence is obtained (if needed) via:


emit k exactly H[k] times


Importantly:

> The order is not constructed. It is already encoded in the address space.

---

## Conceptual Model (with CRN)

             ┌────────────────────┐
             │     Input Lanes    │
             │   k1  k2  k3  k4   │
             └─────────┬──────────┘
                       │
                       ▼
       ┌─────────────────────────────────┐
       │  Conflict Resolution Network    │
       │             (CRN)               │
       │                                 │
       │   [k1==k2?] → merge/add         │
       │   [k3==k4?] → merge/add         │
       │          ↓                      │
       │      reduction tree             │
       └─────────┬───────────────────────┘
                 │
                 ▼
    ┌────────────────────────────┐
    │     Histogram H[k]         │
    │   (canonical state)        │
    └─────────┬──────────────────┘
              │
              ▼
    ┌────────────────────────────┐
    │      Geometric Scan        │
    │       k = 0 ... U-1        │
    └─────────┬──────────────────┘
              │
              ▼
    ┌────────────────────────────┐
    │   Output Vector (optional) │
    └────────────────────────────┘

### Key interpretation

- `H[k]` is **not an intermediate structure**
- It is the **canonical ordered representation**
- The output vector is a **derived projection**

---

## Conflict Resolution Network (CRN)

The CRN resolves concurrent writes without comparisons:


if k_i == k_j → (k, c_i + c_j)


Properties:

- Uses only equality (no `<` or `>`)
- Additive (no information loss)
- Parallel-friendly
- Logarithmic depth reduction

---

## Comparison with Counting Sort

| Property            | Counting Sort        | DialSort                  |
|--------------------|---------------------|---------------------------|
| Prefix sum         | Required            | Not required              |
| Output vector      | Mandatory           | Optional                  |
| Histogram role     | Intermediate        | Canonical state           |
| Parallelism        | Limited             | Native (CRN-based)        |
| Ordering model     | Constructed         | Revealed                  |

---

## Implementations

This repository includes three variants:

- **DialSort-Counting** → sequential baseline
- **DialSort-Parallel** → multi-lane + CRN
- **DialSort-Radix** → full int32 domain support

---

## Experimental Results

Benchmarks were executed under controlled conditions:

- Compiler: `g++ 13.1.0`
- Flags: `-O2 -std=c++17 -pthread`
- Threads: 8
- Measurement: best-of-7 runs
- Warmup: 3 discarded runs
- Seed: `20260321`

### Peak results

- **DialSort-Parallel**
  - Speedup: ~39.77×
  - Throughput: ~115.9 M keys/s

- **DialSort-Counting**
  - Speedup: ~30.26×
  - Throughput: ~88.5 M keys/s

- **DialSort-Radix**
  - Speedup: ~14.6× (full int32 domain)

All correctness checks: **PASSED**

---

## Reproducibility

Full experimental dataset is available:


/results/results_full.csv


Columns:


algorithm,distribution,N,U,time_ms,throughput_mkeys_per_s,speedup_vs_std_sort,correct,best_run_index


The dataset contains all configurations used in the paper.

---

## Build & Run

Compile:

```bash
g++ -O2 -std=c++17 -pthread -o bench dialsort_benchmark.cpp

Run:

./bench
Complexity
Time: O(n + U)
Space: O(U)
Limitations
Requires bounded universe for optimal performance
Performance degrades when U >> n
Output scan cost dominates for large domains
Conceptual Interpretation

DialSort can be understood as a physical system:

Fluidic: flow occurs only where channels are active
Electrical: registers emit when energized
Photonic: resonators couple light when populated

In all cases:

The system reveals state. It does not compute order.

Paper

This repository accompanies the paper:

DialSort: Non-Comparative Integer Sorting via the Self-Indexing Principle

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.19169171.svg)](https://doi.org/10.5281/zenodo.19169171)

Author

Alexander Narváez
Independent Researcher / Universidad EAFIT
Colombia
Email: anarvae1@eafit.edu.co
