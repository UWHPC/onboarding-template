# Performance Engineering Guide — UWHPC Onboarding: 2D Heat Diffusion

> **Purpose.** A practical, evidence-driven playbook for making the stencil fast and
> *provably* fast: the performance model, the build/measurement workflow, the
> concrete optimisations that matter here, the tooling that settles arguments, and
> the pitfalls that silently invalidate results. Read alongside
> [`DESIGN.md`](./DESIGN.md) (the decisions) and [`REQUIREMENTS.md`](./REQUIREMENTS.md)
> (the contracts).
>
> Guiding rule from the upstream guide: *"If something got faster and you cannot say
> why, you do not yet know whether it holds on another machine."*

---

## 1. Where the performance lives: the roofline

The benchmark runs a **1024 × 1024** grid for **200 steps** — about **209.7 million
cell updates**. Each interior update reads five `double`s and writes one. The kernel
does roughly 9 floating-point operations per cell, so its arithmetic intensity is on
the order of **0.5 flop/byte**. This is emphatically **memory-bandwidth bound**, not
compute bound.

**Minimum DRAM traffic per step** (assuming rows are reused from cache while the
neighbouring output rows are computed):

```
read  old grid : 1024 · 1024 · 8 B = 8.00 MiB
write new grid : 1024 · 1024 · 8 B = 8.00 MiB
per step       ≈ 16.0 MiB
200 steps      ≈ 3.36 GB   (3,200 MiB)
```

**Roofline floor:**

```
t_min  ≈  3.36 GB  /  effective_bandwidth
```

| Effective BW | `t_min` | Comment |
| --- | --- | --- |
| 20 GB/s | ≈ 168 ms | naively arranged traffic / weak single core |
| 40 GB/s | ≈ 84 ms | typical dual-channel desktop |
| 80 GB/s | ≈ 42 ms | fast server socket |
| 200 GB/s | ≈ 17 ms | multi-channel / HBM |

The sample reference run in the onboarding docs is `148.4 ms` → about **22.7 GB/s**
effective. That is *far below* a well-tuned machine's bandwidth, which is exactly why
there is headroom: the reference (`vector<vector<double>>`, scalar) wastes bandwidth
on indirection and poor vectorisation.

**Two consequences.**

1. **Speedup is capped by bandwidth, not by cores.** Adding threads helps until
   memory saturates, then stops. The crossover is machine-specific and often lower
   than expected; find it by measuring serial-vectorised vs. parallel.
2. **A score above 1.0 is easy; a high score is a data-path problem.** Optimise how
   bytes move, then add threads.

> Working-set note: two grids are 16 MiB. On a CPU with ~16 MiB L3 the working set
> sits at the cache boundary, so some reuse is possible. In the harness the reference
> grids (another 16 MiB) are also resident, which pushes the total past L3. Measure,
> do not assume.

---

## 2. Baseline first, then one change at a time

The upstream guide's prescribed order:

1. **Correct serial baseline.** Flat layout, scalar interior, no pragmas. Must pass
   all cases.
2. **Alignment, stride, and aliasing made explicit.** Padding, aligned allocation,
   documented no-alias contract.
3. **Vectorised inner loop.** Confirm with a vectorisation report.
4. **Parallelism across rows.** `#pragma omp parallel for`, static.

Rules of the workflow:

- **Change one design decision at a time** so each delta is attributable.
- **Run each configuration several times** and compare distributions, not a single
  favourable sample.
- **Record flags, thread count, and machine** with every number.
- **A fast result does not excuse undefined behaviour, an invalid aliasing promise,
  or unsafe ownership.** Performance found by breaking a contract is not performance.

---

## 3. Build configuration: mirror the evaluator

The evaluator does *not* use your local defaults. Reproduce its build to make local
numbers meaningful.

| Setting | Evaluator | Local check |
| --- | --- | --- |
| Build type | Release | Must be Release — a **Debug build costs most of your score**. |
| Optimisation | `-O3` (preset `CMAKE_CXX_FLAGS_RELEASE`) | Use the preset, never an IDE's default. |
| Architecture | `-march=x86-64-v3` on x86 (AVX2/FMA) | Add it explicitly in a scratch build. |
| OpenMP | **Enabled** | Ensure your compiler finds OpenMP; otherwise pragmas compile serially. |

**Commands (from the repository root):**

```bash
cmake --preset benchmark
cmake --build --preset benchmark
ctest --preset benchmark --output-on-failure
./build/benchmark/uwhpc_benchmark
```

**Scratch build that mirrors the evaluator** (do not commit config changes):

```bash
cmake --preset benchmark -B build/eval -DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=x86-64-v3"
cmake --build build/eval
./build/eval/benchmark/uwhpc_benchmark
```

> **Common trap.** Stock macOS Clang ships without OpenMP, so `#pragma omp` is
> ignored and the code runs serially. Any scaling conclusion drawn from that build is
> meaningless. Install `libomp`, or use a compiler with built-in OpenMP, before
> measuring parallel scaling.

**Reading the score.** `score = reference_ms / submission_ms`, printed as one JSON
line. `> 1.0` beats the naive reference; **`> 0.9` is required to pass**. The
threshold is a floor, not a target. `memory_mb` is a fixed function of grid size and
does not measure your allocation.

---

## 4. Measurement workflow (how to get trustworthy numbers)

1. **Warm up and repeat.** Run the binary 5–10 times; the first run may include page
   faults and frequency ramp-up.
2. **Use distributions.** Report median and spread (min/max), not the best run.
3. **Reduce noise.** Close background workloads; prefer an otherwise idle machine.
   On laptops, watch thermal throttling.
4. **Control thread placement.** `OMP_NUM_THREADS=N`, and consider
   `OMP_PROC_BIND=close` / `OMP_PLACES=cores` when studying scaling.
5. **A/B with one variable.** e.g. `cols` vs padded stride; `-O3` vs `-O3 -march=...`;
   serial vs parallel.
6. **Cross-check with a profiler** (Section 12) before trusting a speedup.
7. **Stay in Release for timing, sanitizers for correctness.** Never time an
   ASan/UBSan/TSan build.

---

## 5. Optimising the data path

Everything else builds on the storage and access path, so settle it first.

| Practice | Effect |
| --- | --- |
| One flat allocation | Removes per-row indirection; inner loop is unit-stride. |
| Contiguous dimension innermost | Enables SIMD and hardware prefetch. |
| Row bases hoisted out of the inner loop | Strength-reduces `i*stride` to one multiply per row. |
| No opaque calls / metadata work per element | Keeps the loop body analyzable. |
| Alignment and aliasing made visible | Lets the compiler vectorise and reorder stores. |
| Logical `cols` separate from physical stride | Rows can be padded/aligned without changing semantics. |

**Anti-patterns that kill the data path:**

- `std::vector<std::vector<double>>` (the reference): pointer chase per row, no
  contiguous guarantee, many small allocations.
- Index expression `*(*(data + i) + j)` or `data[i][j]` where `data` is an array of
  pointers.
- Recomputing `i * cols + j` inside the inner loop and hoping the optimiser hoists it.
- Calling an out-of-line accessor, virtual function, or `std::function` per element.
- Allocating, constructing containers, or building views inside the element update.

---

## 6. Memory: layout, padding, alignment in practice

### 6.1 Width vs. stride

- `cols` is the number of addressable columns.
- `stride` is the number of stored elements between adjacent row starts.
- Recommended: `stride = round_up(cols, 64 / sizeof(double)) = round_up(cols, 8)`.

Padding is **storage only**: it never reaches boundary behaviour or the indexing
interface. Copy only the logical `cols` on the boundary; never let padding influence a
result.

### 6.2 Why round the stride

- If `cols * 8` is not a multiple of the vector/cache-line width, successive rows
  start at different alignments — **even if the base pointer is 64-byte aligned**.
  Rounding the stride makes every row start aligned.
- 64-byte row pitch also prevents false sharing when two threads write the last
  columns of adjacent rows.

### 6.3 The 1024 benchmark, specifically

`1024 * 8 = 8192` bytes is already a multiple of 64, so **padding does not change the
pitch** for the benchmark grid. What still matters is the **base alignment**: a
`malloc`/`std::vector` base is typically only 16-byte aligned, so *every* row is
misaligned by the same amount. A 64-byte-aligned allocation fixes all rows at once.

### 6.4 Verify, do not assume

Check the actual alignment of your allocation, e.g.:

```cpp
// debug-only sanity check
assert(reinterpret_cast<std::uintptr_t>(grid.data()) % 64 == 0);
```

And check what the compiler emits (Section 12). "I used aligned_alloc" is a claim
about the source; the check is a claim about the program.

### 6.5 False sharing

With OpenMP rows and a 64-byte-aligned, 64-byte-multiple stride, two workers writing
adjacent rows do not share a cache line. If the stride were unpadded (e.g.
`cols = 1000` → 8000 bytes, not a multiple of 64), the last columns of one row and
the first of the next could share a line and ping-pong between cores.

---

## 7. SIMD / vectorization in practice

**The column loop is the natural SIMD loop**: consecutive iterations touch
consecutive values. What keeps it analyzable:

- Contiguous dimension innermost.
- Row bases derived outside the loop.
- No opaque calls or per-element metadata work.
- Alignment and aliasing facts visible.
- A scalar remainder when the interior width is not a whole vector length.

**On `#pragma omp simd`.** It asks for vectorisation and asserts no loop-carried
dependence. It will **not** fix an unfriendly layout, and it will **not** make a false
dependency promise true. Use it *after* the layout and aliasing work are done.

**Which streams are aligned?** With a 64-byte base and stride a multiple of 8
doubles:

| Stream | Alignment |
| --- | --- |
| `out_row[j]`, `mid[j]`, `up[j]`, `down[j]` | aligned at `j ≡ 0 (mod 8)`; the loop starts at `j = 1`, so the first vector is off by 8 bytes |
| `mid[j-1]` | centre − 8 bytes — generally unaligned |
| `mid[j+1]` | centre + 8 bytes — generally unaligned |

So **not every load shares an alignment**; the neighbour streams are offset by one
element. In practice the compiler uses unaligned loads (`vmovupd`, `vmovdqu`), which
are cheap on modern x86 when they hit in cache. If a report shows split-line accesses
dominating, consider an **alignment peel**: process the first few scalar `j` so the
vector body begins on a vector boundary. Only do this if measured.

**Intrinsics / experimental SIMD types.** They cost portability and add explicit tail
handling. They earn that only when the vectorisation report and measurement show the
compiler's output is insufficient *and* you commit to the target the evaluator
supports. Prefer the standard, portable form unless the data says otherwise.

**Evidence over belief.** A pragma in the source is no evidence that vector
instructions came out. Read the report; if the compiler declines, it says why.

---

## 8. OpenMP in practice

| Practice | Reason |
| --- | --- |
| Parallelise over **output rows** | Rows are independent; each cell is written once. |
| `schedule(static)` | Uniform row cost; dynamic scheduling only adds overhead here. |
| **One** parallel region per call | The benchmark calls the stencil 200 times; extra forks/joins are paid 200 times. |
| Keep boundary writes inside the row loop | Boundary rows/columns are assigned like any other row — no cell is written twice, no race. |
| Do **not** `std::thread` per call | Constructing/joining threads per call is paid at 200×; OpenMP pools them. |
| Do **not** hardcode thread count | Count belongs to the machine; honour `OMP_NUM_THREADS` and runtime defaults. |
| Avoid nested parallelism | No benefit here; adds overhead and oversubscription risk. |
| Never assume OpenMP is present | A build without it silently runs serially; check your local toolchain. |

**Fork/join cost.** A parallel region has a synchronisation cost. With 200 short
calls this is usually amortised by OpenMP's pool, but it is why "one region per call"
matters and why per-call thread construction is wrong.

**Scaling experiment.** Build and time: (a) scalar serial, (b) vectorised serial,
(c) vectorised parallel with 1, 2, 4, 8, … threads. Plot time vs. threads. The curve
flattens where bandwidth saturates — that is your bandwidth ceiling, and it usually
arrives well before core count. Report the crossover.

---

## 9. Boundary and tail handling

- **Top/bottom rows** are contiguous → `std::memcpy(out.row(0), in.row(0),
  cols * sizeof(double))`. Honest, fast, and expresses "copy unchanged".
- **Left/right columns** for interior rows: two scalar assignments per row.
- **Scalar remainder** for the interior vector loop: handled naturally by
  `for (j = 1; j + 1 < cols; ++j)` with `#pragma omp simd`; the compiler emits a
  scalar epilogue.
- **Padding is not copied.** Copy exactly `cols` logical elements. Copying the whole
  stride is also safe (padding is never read) but less clear.
- **Boundary cost is negligible** (< 1 % of cells for the benchmark); do not
  over-engineer it.

**Tiny grids.** Use `i + 1 < rows` (not `i < rows - 1`) so unsigned arithmetic cannot
wrap when `rows < 2`. `rows < 3` or `cols < 3` means no interior; the loops must
simply not execute.

---

## 10. Numerics and correctness under optimisation

- **Stay in `double`.** Tolerance is `1e-6` absolute; `float` cannot hold the
  accumulated field accurately enough.
- **Do not use `-ffast-math`.** The evaluator does not, and it would licence
  reassociation that changes results and can break the reference comparison.
- **Reordering additions is fine.** The reference computes
  `0.5*centre + 0.125*(up + down + left + right)`. Other association orders differ by
  ~1e-16 relative, far inside `1e-6`. FMA contraction (default in GCC/Clang at `-O3`)
  likewise stays well within tolerance.
- **Preserve the exact weights.** `0.5` and `0.125` are exact binary fractions; no
  rationalisation is needed or wanted.
- **Assert invariants in debug.** A uniform field must stay uniform; the boundary must
  be unchanged across steps.
- **Beware store bypassing.** Non-temporal/streaming stores (`movntpd`) can help a
  pure streaming kernel but bypass cache; here rows are re-read soon after, so
  streaming stores are usually counterproductive. Only try if measurement says so.

---

## 11. Edge cases and hidden tests

The evaluator runs private cases. Protect yourself with a local matrix:

| Dimension | Cases to cover |
| --- | --- |
| Shape | square, `rows > cols`, `cols > rows` |
| Size | `1×1`, `1×N`, `N×1`, `2×2`, `3×3`, large |
| Degenerate | `rows < 3`, `cols < 3` |
| Steps | one step, few steps, many steps |
| Initial condition | center block, linear gradient, checkerboard |
| Values | uniform field (invariant), maximum contrast (checkerboard) |
| Buffers | ping-ponged distinct grids (harness behaviour) |

**The non-square cases are deliberate**: they catch a layout that assumes rows and
columns are interchangeable, and one that confuses a row's width (`cols`) with the
distance between rows (stride). Test `48×80` **and** `80×50` locally.

**Do not treat a successful compilation or a passing public suite as completion.**
The evaluator's comment names only the *category* of a failure, never the exact
hidden case.

---

## 12. Tooling that settles arguments

### 12.1 Vectorisation reports

**GCC:**

```bash
g++ -O3 -march=x86-64-v3 -fopt-info-vec-optimized -fopt-info-vec-missed -c bench/main.cpp -I src -o /dev/null
```

**Clang:**

```bash
clang++ -O3 -march=x86-64-v3 -Rpass=loop-vectorize -Rpass-missed=loop-vectorize -c bench/main.cpp -I src -o /dev/null
```

Look for the interior loop; note vector width and whether the compiler chose aligned
or unaligned loads, and why if it declined (aliasing, unknown trip count, remainder).

### 12.2 Assembly

```bash
objdump -d --no-show-raw-insn build/benchmark/uwhpc_benchmark | less
```

Check for packed instructions (`vfmadd...pd`, `vmovupd`, `vaddpd`) in the stencil
body, and confirm the kernel inlined. "Building both forms and comparing the output
settles it."

### 12.3 Hardware counters

```bash
perf stat -e cycles,instructions,cache-misses,cache-references,LLC-load-misses ./build/benchmark/uwhpc_benchmark
perf record -g ./build/benchmark/uwhpc_benchmark && perf report
```

Estimate achieved bandwidth and IPC; identify whether you are bandwidth-saturated.
Alternatives: `likwid-perfctr`, Intel VTune, `cachegrind`/`callgrind`.

### 12.4 Sanitizers (correctness, not speed)

```bash
# Address + UB (serial or parallel)
cmake --preset benchmark -B build/asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
cmake --build build/asan && ./build/asan/benchmark/uwhpc_benchmark --check

# Thread sanitizer (parallel correctness)
cmake --preset benchmark -B build/tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
cmake --build build/tsan && ./build/tsan/benchmark/uwhpc_benchmark --check
```

ASan/UBSan catch out-of-bounds and UB; TSan catches data races and false sharing
mistakes. **Never benchmark a sanitizer build.**

---

## 13. Anti-patterns that cost points

| Anti-pattern | Why it hurts |
| --- | --- |
| Debug build for benchmarking | Loses most of the score. |
| Single benchmark run | Noise dominates; conclusions unreliable. |
| Local build without OpenMP | Parallel code silently serial; scaling conclusions invalid. |
| Hardcoded thread count | Behaves differently on the evaluator machine. |
| `std::thread` created per call | Paid 200×; no pooling. |
| `vector<vector<double>>` | Pointer chasing, no unit stride. |
| Computing `i*cols+j` per element | Relies on the optimiser; explicit bases are safer. |
| Allocation / container ops in the element loop | Per-cell cost. |
| Assuming `rows == cols` or `stride == cols` | Fails non-square hidden cases. |
| `i < rows - 1` with unsigned `i` | Wraps for tiny grids. |
| Copying padding into logical cells | Changes results. |
| `restrict` while grids alias | Undefined behaviour, no diagnostic. |
| `-ffast-math` | Licences reassociation; not used by the evaluator. |
| Leftover debug prints / commented experiments | Noise in a file reviewed for design. |

---

## 14. Tuning checklist

**Correctness**

- [ ] Serial baseline passes all five public cases.
- [ ] Local matrix: non-square both orientations, tiny/degenerate, one step.
- [ ] Uniform-field invariant holds; boundary unchanged across steps.
- [ ] ASan/UBSan clean; TSan clean for the parallel path.

**Data path**

- [ ] Single flat allocation; unit stride in the inner loop.
- [ ] `stride` separate from `cols`; padded to 64 bytes.
- [ ] Base pointer verified 64-byte aligned.
- [ ] Row bases hoisted out of the inner loop.

**SIMD**

- [ ] Vectorisation report shows the interior loop vectorised.
- [ ] Assembly contains packed FP instructions in the kernel.
- [ ] Scalar remainder correct for odd interior widths.
- [ ] No false aliasing promise; `restrict` justified and documented.

**Parallelism**

- [ ] OpenMP actually enabled in the build used for measurement.
- [ ] `parallel for schedule(static)` over rows; one region per call.
- [ ] No cell written twice; boundary folded into the row assignment.
- [ ] Thread count left to the runtime; `OMP_NUM_THREADS` respected.
- [ ] Scaling curve measured; bandwidth crossover identified.

**Process**

- [ ] Release build; evaluator-like flags; multiple runs; distributions reported.
- [ ] One variable changed at a time; each result reproducible.
- [ ] Score `> 0.9` locally, with margin for machine variance.

---

## 15. Sanity targets and what to report

- **Traffic floor:** ≈ 3.36 GB per full benchmark run.
- **Time floor:** `3360 MB / effective_BW`. Measure your machine's BW (e.g. with a
  STREAM-like loop or from `perf`).
- **Expected shape of results:** serial-flat beats the naive reference substantially;
  vectorisation adds a modest amount; multithreading helps until bandwidth saturates.
- **Report per configuration:** median runtime, spread, score, flags, thread count,
  and the reason you believe the number. A number without a reason is not a result.

> Final word from the guide: *"A submission that departs from this guide for a reason
> reads better than one that follows it without one."* Measure, explain, and be ready
> to defend the delta.

---

## 16. Tips-and-tricks summary (one screen)

1. **Release + `-O3`**, and mirror `-march=x86-64-v3` and OpenMP locally.
2. **Flat, aligned, padded** storage; `stride ≠ cols`.
3. **Row bases hoisted**; contiguous innermost; no per-element metadata.
4. **`restrict` at the kernel boundary only**, with a documented no-alias contract.
5. **Let the compiler vectorise**; verify with a report and the assembly; align-peel
   only if the data says so.
6. **OpenMP `parallel for schedule(static)` over rows**; one region per call; no
   hardcoded thread count; no per-call `std::thread`.
7. **`memcpy` boundary rows; scalar boundary columns; guard tiny grids** with
   `i + 1 < rows`.
8. **Never `-ffast-math`; stay in `double`;** reordering and FMA are safe within
   `1e-6`.
9. **Sanitise for correctness, benchmark in Release.**
10. **Measure distributions, change one thing, and explain every number.**
