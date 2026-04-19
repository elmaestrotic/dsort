# 🚀 DialSort — Non-Comparative Integer Sorting via the Self-Indexing Principle

> **DialSort does not compute order. It reveals it.**

---

## 🧠 Overview

DialSort is a high-performance non-comparative sorting approach for bounded-universe integer keys.

Instead of comparing elements, it leverages the **self-indexing property of integers**, where each key *k* is inherently its own position in the ordered address space.

Sorting becomes:
- Direct indexing (ingestion)
- Local accumulation (histogram)
- Geometric scan (order revelation)

No pairwise comparisons are performed.

---

## ⚙️ Core Idea

For keys `k ∈ [0, U-1]`:

```cpp
H[k]++;
```

```cpp
for (int k = 0; k < U; k++)
    emit k exactly H[k] times;
```

👉 The histogram **is already the ordered state**.

---

## ✨ Geometric Interpretation

After ingestion, the dataset is represented as a histogram `H[k]`, where each value occupies its own vertical rail.

### 🔦 Abacus-and-Torch Analogy

![DialSort Geometric Scan](./dialsort_abacus_torch.png)

**Figure:** After ingestion, each value *k* occupies exactly `H[k]` independent rails.  
A torch sweeps the value axis from `k = 0` to `U − 1`. Repeated values appear side-by-side at the same height.

👉 The projected shadow is the sorted output.

> **Order is revealed, not computed.**

---

## ⚡ Performance Highlights

### 🔹 Parallel Benchmark — DialSort vs IPS4o

- 48 configurations (N, U, distributions)
- 8 threads vs 8 threads (fair comparison)

**Results:**
- Wins in **29 / 48 configurations**
- Average speedup: **1.90×**
- Best case: **4.08×**

---

### 🔹 vs Classic Counting Sort

- Faster in **46 / 48 cases**
- ~1.65× average speedup

---

### 🔹 vs ska sort

- Faster in **46 / 48 cases**
- ~3.33× average speedup

---

## 🧩 Cerebras WSE-3 Projection (900,000 cores)

DialSort maps naturally to wafer-scale architectures:

- One tile per key
- Each tile stores `H[k]`
- Direct routing to owner tile

### Key insight:
- Ingestion latency is bounded by hardware topology
- No global synchronization required

```
T_total = T_transfer + O(U)
```

⚠️ **Important:** This is an *analytical projection*, not a physical implementation.

---

## 🔬 Complexity

| Mode        | Complexity            |
|------------|----------------------|
| Sequential | O(n + U)             |
| Parallel   | O(n/k + log k + U)   |

---

## 📌 When to Use DialSort

Use DialSort when:
- Keys are integers in a bounded range
- `n ≫ U`
- Distribution is uniform or skewed

Avoid when:
- `U ≫ n`
- Input is already sorted

---

## 📦 Repository Contents

- C++ implementations (sequential + parallel)
- Benchmarks (DialSort vs IPS4o, ska sort, std::sort)
- Experimental datasets
- Interactive simulators
- Paper (PDF)

---
## 🎮 Interactive Simulators

DialSort can be explored through multiple complementary visual models.  
Each simulator highlights a different layer of the execution model:

---

### 🔷 Sovereign Dial Model — Geometric Ordering
Visualizes the core principle of DialSort:  
each value *is its own position* in the ordered space.

👉 https://elmaestrotic.github.io/dsort/simulators/sovereign-dial/

---

### ✨ Luminico Model — Frequency as Illumination
Represents frequency accumulation as light intensity along value rails.  
Higher repetition → stronger illumination.

👉 https://elmaestrotic.github.io/dsort/simulators/luminico/

---

### 🧠 Spatial / Cerebras Model — Parallel Execution
Simulates a many-core architecture where each key is routed to its owning tile.  
Demonstrates how DialSort maps naturally to massively parallel hardware.

👉 https://elmaestrotic.github.io/dsort/simulators/cerebras/

---

### 🔥 CRN Simulator — Conflict Resolution Network
Shows how concurrent writes are resolved without collisions.  
Equal keys are merged through a reduction tree before reaching memory.

👉 https://elmaestrotic.github.io/dsort/simulators/crn/

---

## 🧩 Conceptual Layers

DialSort can be understood through multiple equivalent lenses:

- **Geometric** → Sovereign Dial  
- **Energetic** → Luminico  
- **Architectural** → Spatial (Cerebras-style)  
- **Algorithmic** → CRN  

> These perspectives are not separate implementations, but different views of the same underlying execution principle.
## 🧪 Reproducibility

- Seed: `20260321`
- Compiler: `g++ -O3 -std=c++17`
- Parallel: `pthread` / `TBB`

---

## 👤 Author

**Alexander Narváez**  
Universidad EAFIT — Colombia
