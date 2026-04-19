
# DialSort — Non-Comparative Integer Sorting via the Self-Indexing Principle

> **DialSort does not compute order. It reveals it.**

---

## 🚀 Overview

DialSort is a high-performance non-comparative sorting approach for bounded-universe integer keys.  
Instead of comparing elements, it leverages the **self-indexing property of integers**, where each key naturally encodes its position in the ordered space.

Sorting becomes:
- **Direct indexing (ingestion)**
- **Local accumulation (histogram)**
- **Geometric scan (order revelation)**

No pairwise comparisons are performed.

---

## ⚙️ Core Idea

For keys `k ∈ [0, U-1]`:

1. Build histogram:
```
H[k]++
```

2. Scan:
```
for k in 0..U:
    emit k exactly H[k] times
```

👉 The histogram **is already the ordered state**.

---

## 🧠 Key Contributions

- Self-indexing principle formalized
- Conflict Resolution Network (CRN) for parallel ingestion
- Elimination of prefix-sum phase
- In-place output (no scatter buffer)
- Substrate-aware architecture (CPU → FPGA → Photonic → WSE-3)

---

## ⚡ Performance Highlights

### 🔹 Parallel Benchmark — DialSort vs IPS4o

- 48 configurations (N, U, distributions)
- 8 threads vs 8 threads (fair comparison)

**Results:**
- DialSort wins in **29 / 48 configurations**
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

DialSort maps naturally to wafer-scale architectures.

- One tile per key
- Local histogram per tile
- Constant-bounded routing latency

⚠️ Analytical projection (not physical implementation)

---

## 🔬 Complexity

| Mode        | Complexity        |
|------------|------------------|
| Sequential | O(n + U)         |
| Parallel   | O(n/k + log k + U) |

---

## 📌 Conclusion

DialSort transforms sorting into a **geometric memory process**, eliminating comparisons and enabling high parallel efficiency.

---

## 👤 Author

Alexander Narváez
