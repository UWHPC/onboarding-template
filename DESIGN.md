# Design — UWHPC Onboarding: 2D Heat Diffusion

> **Purpose.** This document records the *design decisions* behind the submission,
> the alternatives that were rejected, and the trade-offs each choice carries. It is
> written to be defensible in the virtual design chat: every decision is stated as a
> contract with a reason, and every performance-sensitive claim is tied to the
> generated code rather than to how short the source looks.
>
> Companions: [`REQUIREMENTS.md`](./REQUIREMENTS.md) (what must be true) and
> [`PERFORMANCE.md`](./PERFORMANCE.md) (how performance is pursued and measured).

---

## 1. Design goals, in priority order

1. **Correctness first.** All public and hidden cases pass within `1e-6`, for every
   shape including degenerate ones.
2. **A narrow, auditable kernel boundary.** The numeric hot loop should be a few
   lines over flat pointers, with alignment and aliasing facts visible and checkable
   in one place.
3. **Performance bounded by the data path,** not by abstraction. The problem is
   memory-bandwidth bound; the design must not add work per element.
4. **Clear ownership and lifetime.** Allocation, dimensions, stride, and lifetime
   belong to one type; the kernel borrows.
5. **Localised change.** Changing the layout, the work distribution, or the boundary
   strategy should touch one region of the file, not most of it.
6. **Portable, standard C++17.** No compiler-specific intrinsics unless measurement
   justifies them.

---

## 2. Constraints that shaped the design

| Constraint | Design consequence |
| --- | --- |
| Only `src/submission.hpp` may change | Header-only, `#pragma once`, everything `inline` or class-local. |
| `Grid` interface is fixed | Keep the two `operator()` overloads and the constructor exactly; extend *around* them. |
| Harness uses only `operator()` | Internal layout is free; the kernel uses internal accessors, not `operator()`. |
| `apply_stencil(const Grid&, Grid&)` | Read-only input, mutable output, two buffers; ownership stays in `Grid`. |
| Benchmark = 1024² × 200 steps, memory-bound | Favour streaming access, minimal traffic, and parallelism across independent rows. |
| Evaluator: `-O3`, `-march=x86-64-v3`, OpenMP | Rely on auto-vectorisation and the OpenMP work-sharing pool; do not hand-roll threads. |
| Non-square and tiny hidden cases | Keep stride independent of `cols`; guard all loops against unsigned underflow. |

---

## 3. Component decomposition

Responsibilities are kept narrow, and each phase can be inspected on its own:

| Component | Responsibility | Owns memory? |
| --- | --- | --- |
| `Grid` | Allocation, dimensions, stride, lifetime, element access. | **Yes** |
| `ConstGridView` / `GridView` | Non-owning access: pointer + dimensions + stride; inlined accessors. | No |
| `copy_boundary(in, out)` | Preserve the problem contract on the outer ring. | No |
| `stencil_interior(in, out)` | The numeric five-point update on interior rows. | No |
| `apply_stencil` | Orchestration: build views, handle boundary, launch the parallel row loop. | No |

The litmus test: to change the memory layout you touch `Grid` and the views; to
change the work distribution you touch only `apply_stencil`. Neither change forces
edits to the numeric kernel.

> Why not keep thread counts or row partitions in `Grid`? Because those describe
> *how the work runs*, not *who owns the field*. Coupling them would mean changing
> one whenever the other changes, and would make `Grid` less reusable (e.g. for a
> serial reference).

---

## 4. Decision D1 — Storage layout: one flat allocation

**Choice.** A single contiguous, row-major, aligned allocation of
`rows × stride` `double`s, addressed as `data[i * stride + j]`.

**Why.**

- **Unit stride in the inner loop.** Consecutive `j` are consecutive addresses,
  which is the precondition for SIMD and for the hardware prefetcher.
- **No indirection.** Unlike `vector<vector<double>>`, there is no pointer-chase
  per row: the row base is `data + i * stride`, pure arithmetic.
- **The layout is visible.** The compiler and the reader see one base and one
  pitch, so vectorisation reports and assembly are interpretable.
- **One allocation** means one ownership decision, one deallocation, and one place
  to align.

**Rejected alternatives.**

| Alternative | Why rejected |
| --- | --- |
| `std::vector<std::vector<double>>` | Per-row allocations, pointer chasing, no unit-stride guarantee across a row, 5 separate heap blocks read per cell; this is essentially the naive reference. |
| Row-major `i * cols + j` with no stride field | Cannot pad or align rows independently of `cols`; conflates logical width with physical pitch. |
| Column-major | The update is a row-wise stencil; row-major keeps the innermost accesses contiguous. |
| `std::map` / sparse | Grids are dense; hashing/indirection per cell is pure overhead. |

---

## 5. Decision D2 — Stride, padding, and alignment

**Choice.** Introduce a physical **stride**, distinct from `cols`:

```
stride = round_up(cols, kSimdWidth)   // kSimdWidth = 64 / sizeof(double) = 8
```

and allocate `rows * stride` elements from a **64-byte-aligned** base.

**Why width ≠ stride.** `cols` is what a caller may address; the stride is how many
stored elements separate adjacent row starts. Rounding the stride to a whole
cache-line / SIMD unit makes **every row start at the same alignment**. Aligning only
the base pointer is not enough: if `cols * sizeof(double)` is not a multiple of the
alignment, successive rows drift out of alignment.

**What padding buys.**

1. **Aligned row starts** — enables aligned vector loads/stores at the start of each
   row and avoids split cache-line accesses at row boundaries.
2. **False-sharing avoidance** — with 64-byte row pitch, two threads writing the
   last columns of adjacent rows do not share a cache line.
3. **Tail room for the vector loop** — the scalar remainder is handled in storage
   that exists anyway.

**On the 1024 benchmark specifically.** `1024 * 8 = 8192` bytes, already a multiple
of 64, so padding does **not** change the *pitch* of the benchmark grid. What still
matters there is the **base alignment**: without a 64-byte-aligned allocation, every
row (at a shared misalignment) is misaligned. Padding matters more for non-square
shapes and for portable alignment guarantees.

**Honest caveat.** On modern x86, unaligned loads that hit in cache are cheap, and
compilers emit `vmovupd` regardless. Alignment is justified primarily by
auditability, by split-line avoidance on large streams, and by false-sharing
avoidance — not by a large per-instruction win. This is stated as a judgment, not a
magic incantation.

**Invariant.** Padded elements are **storage only**. They lie outside the logical
grid and must never influence a result: the boundary routine copies only logical
columns, and the interior loop never reads or writes them.

---

## 6. Decision D3 — Ownership and RAII

**Choice.** The `Grid` owns its buffer through an RAII member. Recommended: a
`std::vector<double, AlignedAllocator<double, 64>>`, giving alignment *and* the
rule of zero. Equivalent alternative: a `std::unique_ptr<double[], AlignedDelete>`
created with aligned `new`/`delete`.

**Why RAII.** A raw owning pointer leaves every failure path (constructor throw,
early return, copy) to be handled by hand. The performance guide puts it directly:
"the problem RAII exists to solve." RAII gives:

- Allocation and deallocation paired exactly.
- Zero initialisation preserved (`vector(count, 0.0)` or `new double[n]()`).
- Allocation failure propagated as `std::bad_alloc`, not a null dereference.
- Copy/move generated or declared once.

**Aligned allocation preconditions.** C11 `std::aligned_alloc` requires the size to
be a multiple of the alignment; `posix_memalign` does not. C++17 aligned `operator
new`/`delete` take an explicit `std::align_val_t{64}` and must be paired with the
aligned form of `delete`. Whichever is used, the size and alignment preconditions
are met (round the byte count up, or guarantee the element count is a multiple of
the alignment in `double`s).

**Sketch (illustrative, to be compiled and sanitised):**

```cpp
namespace detail {

constexpr std::size_t kAlignment = 64;                       // cache line
constexpr std::size_t kSimdWidth = kAlignment / sizeof(double);  // 8 doubles

inline std::size_t round_up(std::size_t v, std::size_t m) noexcept {
  return ((v + m - 1) / m) * m;
}

template <class T, std::size_t Align>
struct AlignedAllocator {
  using value_type = T;

  AlignedAllocator() noexcept = default;
  template <class U>
  AlignedAllocator(const AlignedAllocator<U, Align>&) noexcept {}

  [[nodiscard]] T* allocate(std::size_t n) {
    if (n == 0) return nullptr;
    return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{Align}));
  }
  void deallocate(T* p, std::size_t) noexcept {
    ::operator delete(p, std::align_val_t{Align});
  }

  template <class U> struct rebind { using other = AlignedAllocator<U, Align>; };
  friend bool operator==(const AlignedAllocator&, const AlignedAllocator&) noexcept { return true; }
  friend bool operator!=(const AlignedAllocator&, const AlignedAllocator&) noexcept { return false; }
};

}  // namespace detail
```

> **Note.** `#pragma omp simd` / align attributes do not fix an unaligned allocator;
> the alignment must be real at allocation time.

---

## 7. Decision D4 — Value semantics

**Choice.** `Grid` is **movable** (cheap pointer/vector move) and either **deep-copyable**
or explicitly non-copyable; the choice is stated, not left implicit.

**Why it matters.** The harness constructs grids by value and passes them by
reference. The rule of five must be coherent: if you hand-write a destructor or
owning pointer, you must also decide copy and move. With `std::vector` or
`std::unique_ptr` members, the rule of zero already gives correct move and (for
`vector`) correct deep copy. If copy is disabled, say so in a comment so a reviewer
knows it is intentional.

**Recommendation.** Prefer rule-of-zero (owning standard container) so no hand-written
destructor, copy, or move exists to get wrong. If a custom buffer is used, write all
five or delete copy explicitly.

---

## 8. Decision D5 — Views: separate ownership from access

**Choice.** Two small non-owning value types:

```cpp
class ConstGridView {                 // read-only
  const double* data_;
  std::size_t rows_, cols_, stride_;
public:
  double  operator()(std::size_t i, std::size_t j) const noexcept {
    return data_[i * stride_ + j];
  }
  const double* row(std::size_t i) const noexcept { return data_ + i * stride_; }
  std::size_t rows()   const noexcept { return rows_; }
  std::size_t cols()   const noexcept { return cols_; }
  std::size_t stride() const noexcept { return stride_; }
};

class GridView {                      // mutable
  double* data_;
  std::size_t rows_, cols_, stride_;
public:
  Grid& operator()(...) / double* row(...) / accessors ...
};
```

**Why a view at all.** `Grid` owns the allocation and its lifetime; the kernel only
needs access. A view:

- Holds **pointer + metadata** (dimensions and stride), so those values travel
  together and cannot be accidentally swapped.
- Makes the mutable/read-only distinction a **type**, so "which side is writable"
  is checked by the compiler.
- Is a trivially copyable value: constructing it is a few register moves, so the
  cost is zero after inlining.

**Hard rule.** A view that allocates or copies elements on construction has stopped
being a view, and the cost lands in the hot path. Construction and destruction of a
view must be `noexcept` and allocation-free.

**Why rows and cols do not travel apart.** They are always needed together to
interpret the buffer, and the stride is needed with them. Grouping them means one
argument (a view), one contract, and no chance of passing a mismatched pair.

**3D thought experiment.** If the field were three-dimensional, only the view's
metadata and the index arithmetic change; `Grid`'s ownership and the kernel's loop
nest gain one dimension. A `Shape { rows, cols, (depth) }` + pitch grouping would
localise even that. This is why the metadata is grouped rather than passed as bare
`std::size_t` arguments.

---

## 9. Decision D6 — Aliasing: an explicit, narrow contract

**The problem.** A C++ reference tells the optimiser nothing about whether two grids
occupy disjoint storage. `apply_stencil(const Grid&, Grid&)` therefore forces the
compiler to assume the buffers *may* overlap, which can inhibit vectorisation and
store/load reordering.

**What the harness actually guarantees.** Ping-pong storage: `old` and `new` are two
distinct `Grid` objects constructed separately, and the harness swaps which is old
each step. They are never the same object and never the same buffer.

**Choice.** Record that guarantee as a documented precondition and make it visible
where it can be checked — at the kernel boundary:

- Declare `apply_stencil`'s contract: `old_grid` and `new_grid` must be distinct
  grids that do not share storage. Debug-only `assert(&old_grid != &new_grid)` (and,
  when cheap, a pointer-range disjointness check) makes the promise auditable.
- In the inner kernel, take `double* __restrict__` / `const double* __restrict__`
  row pointers. This is a **correctness contract**, not a hint: if the grids
  aliased, `restrict` would be undefined behaviour.

**Which pointers are restricted, and what each may touch.** The output row pointer
and the three input row pointers are restricted with respect to one another. Even
row pointers taken from one allocation can alias as far as the language is
concerned at different offsets, so the disjointness between `in` and `out`
allocations is what makes the qualifier true. Both facts are stated in a comment
right above the loop.

**What the kernel would do if handed the same grid twice.** It would be undefined
behaviour — reads and writes would interleave within the same buffer and the
`restrict` promise would be broken. The precondition is therefore part of the
function's contract, asserted in debug builds. This is exactly why the promise is
kept narrow: spread further out, it becomes a promise nobody can audit.

---

## 10. Decision D7 — Boundary handling

**Choice.** Copy the boundary verbatim, folding it into the same parallel row loop
that does the interior:

- Rows `0` and `rows-1`: copy the logical columns, `memcpy(out.row(i), in.row(i),
  cols * sizeof(double))` — one contiguous block.
- For each interior row: copy `j = 0` and `j = cols-1` from the input, then run the
  interior column loop.

**Why.**

- The contract is "copy unchanged", and a bulk copy expresses exactly that.
- Top/bottom rows are contiguous, so `memcpy` is the honest description and is well
  optimised by the library.
- Folding boundary work into the row loop means **one parallel region per call**,
  avoiding a second fork/join.
- The boundary is ~`2·(rows + cols)` cells out of `rows·cols` — well under 1 % for
  the benchmark, so elaborate boundary fusion is not worth the complexity.

**Important.** Padding elements are *not* copied. Copying only the logical columns
keeps padding out of the semantic picture. (Copying the whole stride would also be
safe — padding is never read — but copying exactly `cols` is clearer and cannot
accidentally promote a padding value to a logical one.)

---

## 11. Decision D8 — Interior kernel structure

**Choice.** Row-major loop with row bases hoisted out of the inner loop:

```cpp
void stencil_interior(ConstGridView in, GridView out) {
  const std::size_t rows = in.rows();
  const std::size_t cols = in.cols();

  #pragma omp parallel for schedule(static)
  for (std::size_t i = 0; i < rows; ++i) {
    double* __restrict__ out_row = out.row(i);

    if (i == 0 || i + 1 == rows) {                 // boundary rows
      std::memcpy(out_row, in.row(i), cols * sizeof(double));
      continue;
    }

    const double* __restrict__ up   = in.row(i - 1);
    const double* __restrict__ mid  = in.row(i);
    const double* __restrict__ down = in.row(i + 1);

    out_row[0] = mid[0];                            // boundary columns
    out_row[cols - 1] = mid[cols - 1];

    #pragma omp simd
    for (std::size_t j = 1; j + 1 < cols; ++j) {
      out_row[j] = 0.5   * mid[j]
                 + 0.125 * (up[j] + down[j] + mid[j - 1] + mid[j + 1]);
    }
  }
}
```

**Why this shape.**

- **Contiguous dimension innermost**, so consecutive iterations are consecutive
  addresses.
- **Row bases derived outside the inner loop**, so the index arithmetic is
  strength-reduced to pointer increments once per row, not per cell.
- **No opaque calls, no per-element metadata** inside the loop; all metadata work is
  per-row or per-call.
- **Guard `i + 1 < rows`** (equivalently `i + 1 == rows`) rather than `i < rows - 1`
  to stay safe for tiny grids under unsigned arithmetic.
- The same loop handles boundary rows and interior rows, so the parallel region is
  launched once.

**Rejected alternatives.**

| Alternative | Why rejected |
| --- | --- |
| `data[i * cols + j]` with the multiply in the loop | Relies on the optimiser to strength-reduce; explicit row bases make the intent unambiguous. |
| Separate boundary loops before a parallel interior loop | Two parallel regions per call (extra fork/join), and more places to get the ring wrong. |
| In-place update | The stencil reads neighbours; updating in place corrupts them. Requires the ping-pong buffers the harness supplies. |
| Cache/temporal blocking | No reuse *within* a step beyond immediate neighbours, and ping-pong defeats cross-step reuse; added complexity without measured benefit here. See [`PERFORMANCE.md`](./PERFORMANCE.md) for the roofline reasoning. |

---

## 12. Decision D9 — SIMD strategy

**Choice.** Let the compiler vectorise the interior loop, and assert the
no-dependency fact with `#pragma omp simd`. Provide a scalar remainder by
construction (the `j + 1 < cols` bound); do **not** hand-write intrinsics unless a
vectorisation report and measurement say the compiler's output is insufficient.

**Why.**

- The loop is already vectorisation-friendly: contiguous inner index, hoisted row
  bases, no opaque calls, known alignment, and a documented no-alias contract.
- `#pragma omp simd` asks for vectorisation and asserts no loop-carried dependence.
  It will **not** fix an unfriendly layout and will **not** turn a false dependency
  promise true — hence the layout and aliasing work first.
- Intrinsics and experimental SIMD types cost portability and add tail handling;
  they earn that only when measurement demands them. The C++17 evaluator supports
  what is chosen, but portability still matters for local builds.

**Which of the five streams are aligned.** With a 64-byte base and
`stride % 8 == 0` (8 doubles = 64 bytes):

| Stream | Address | Aligned at vector starts? |
| --- | --- | --- |
| `mid[j]` (centre) | `base + i·stride + j` | Yes at `j ≡ 0 (mod 8)`; the loop starts at `j = 1`, so the first vector is misaligned by 8 bytes. |
| `up[j]`, `down[j]` | `base + (i±1)·stride + j` | Same alignment as the centre stream (stride is a multiple of the vector width). |
| `mid[j-1]` (left) | centre − 1 | Offset by −8 bytes; generally misaligned. |
| `mid[j+1]` (right) | centre + 1 | Offset by +8 bytes; generally misaligned. |
| `out_row[j]` | `out_base + i·stride + j` | Same as the centre stream. |

So **not every load shares an alignment**: the neighbour streams sit one element
apart. In practice the compiler uses unaligned loads for those; an alignment peel
(handle the first few `j` scalars so the vector body starts on a vector boundary)
can help but is only worth it if measured. This is stated because the performance
guide asks it directly ("Which of your five streams are aligned, and which are
not?").

---

## 13. Decision D10 — Parallelism with OpenMP

**Choice.** `#pragma omp parallel for schedule(static)` over output rows; rely on
OpenMP's managed thread pool; do not hardcode a thread count; do not construct
`std::thread` per call.

**Why.**

- **Independent output rows** split cleanly. A worker reads any input row, but two
  workers must never write the same cell, so the row is the natural unit of work.
- **Static scheduling** suits uniform row costs; dynamic scheduling buys nothing
  here and costs synchronisation.
- **The benchmark calls the stencil 200 times**, so anything built per call is built
  200 times. Constructing and joining `std::thread` objects inside `apply_stencil`
  is paid at that rate; OpenMP already handles pooling, scheduling, and thread
  count.
- **Thread count belongs to the machine, not the submission.** A hardcoded count
  behaves differently everywhere except where it was chosen. Let the runtime decide
  (and honour `OMP_NUM_THREADS`, which the evaluator may set).
- **Nested parallelism and dynamic scheduling each cost something**, and with
  uniform rows there is little for dynamic scheduling to recover. Nesting is
  disabled in the runtime / not requested.

**Boundary writes and the race rule.** "Two workers writing the same cell is a race
whatever else is true." Here each row is written by exactly one worker; boundary
rows and boundary columns are assigned to the same row loop, so no cell is written
twice. No separate unguarded boundary phase exists to race with the interior.

**Scaling expectation.** Speedup is capped by memory bandwidth, not arithmetic.
Comparing a serial vectorised build against the parallel one shows where extra
threads stop paying — lower than most people expect. See
[`PERFORMANCE.md`](./PERFORMANCE.md).

---

## 14. Zero-cost-abstraction audit

Abstraction earns its place only if it makes contracts visible without adding work
to the hot path. Each abstraction is checked against its generated code:

| Abstraction | Expected after inlining | Verification |
| --- | --- | --- |
| `GridView::row(i)` | `base + i * stride` (one multiply per row) | assembly |
| `GridView::operator()` in the kernel | not used in the hot loop; raw row pointers are | source |
| `Grid::operator()` for harness I/O | `base + i*stride + j` | assembly / not hot |
| View construction | a few register moves | assembly |
| `ConstGridView` vs `GridView` | identical code, different types | compile-time only |

**Per-element traps to avoid** (each is a per-cell cost):
allocation, bounds discovery, virtual dispatch, container copying, rebuilding views
or layout metadata inside the element update. All such work is hoisted to once per
call or once per row.

**The claim is about generated code, not source length.** If the compiler does not
produce the expected form, the interesting question is what it could not see
through — most often aliasing or an opaque call.

---

## 15. Header-only hygiene

Because the deliverable is a header:

- `#pragma once` at the top.
- All free functions and helpers `inline` (or in a named `detail` namespace with
  `inline`), so multiple translation units including the header do not violate ODR.
  Avoid anonymous namespaces in a header: they give each TU a private copy and can
  cause code bloat and confusing behaviour.
- No `main`, no static mutable state, no debug prints, no commented-out experiments.
- Comments record **why**, not **what**. The numeric formula is self-evident; the
  alignment/aliasing/ownership rationale is not.

---

## 16. Extensibility

| Change | Blast radius under this design |
| --- | --- |
| Different memory layout (e.g. blocked) | `Grid` + view accessors. |
| Different work distribution (e.g. column tiles) | `apply_stencil` only. |
| Add a second stencil / operator | New kernel over the same views. |
| Go 3D | Add a depth dimension to the shape metadata and one loop level; ownership and views generalise. |
| Different alignment target | One constant. |
| Replace OpenMP with another runtime | `apply_stencil` only; the kernel is unchanged. |

This is the concrete answer to "how much of the file would you touch to change one
decision?" — one region, because ownership, access, boundary, numeric work, and
orchestration are separated.

---

## 17. Decision log (summary)

| ID | Decision | Alternative(s) rejected | Primary reason |
| --- | --- | --- | --- |
| D1 | Single flat row-major allocation | nested vectors, column-major | unit stride, no indirection |
| D2 | Padded stride (64 B) + aligned base | `i*cols+j`, unaligned base | row alignment, false sharing, tail room |
| D3 | RAII owning buffer | raw owning pointer | exception safety, pairing |
| D4 | Rule-of-zero value semantics | hand-written rule of five | fewer failure modes |
| D5 | Non-owning views | pass `Grid&` everywhere | narrow contract, zero cost |
| D6 | Documented no-alias + `restrict` | ignore aliasing | enables vectorisation, assertable |
| D7 | Verbatim boundary copy, folded in row loop | separate boundary passes | one parallel region, clarity |
| D8 | Row-major kernel, hoisted bases | in-loop index arithmetic | analyzable, vectorisable |
| D9 | Compiler SIMD + `omp simd` | hand intrinsics | portability, verifiability |
| D10 | OpenMP `parallel for` static | `std::thread` per call | pool reuse, machine-independent |
| D11 | Grouped metadata via views | bare `rows, cols, stride` args | cannot transpose dims by accident |
| D12 | Header hygiene: `inline`, no anon ns | anon namespace helpers | ODR safety |

---

## 18. Talking points for the design review

Be ready to answer these from your own reasoning, with evidence:

1. **Requirements interpretation.** Which parts of the interface were fixed, which
   were free, and how did you decide what to expose (dimensions, stride, views)?
2. **Layout.** Why flat and row-major? What does padding change for the 1024
   benchmark vs. a non-square grid? What is the difference between `cols` and stride?
3. **Alignment.** What alignment does the allocation actually have, and how did you
   verify it? Which streams are aligned and which are not?
4. **Aliasing.** How do you know `old_grid` and `new_grid` do not alias? Where is
   that promise made, and what happens in debug builds if it is violated?
5. **Ownership.** Who frees the buffer, which side of the kernel is writable, and who
   keeps storage alive across the call?
6. **Correctness.** How did you verify serial vs. parallel, and against the
   reference? Which extra edge cases did you test beyond the public five?
7. **Performance.** What did you measure, on what build flags, and how many runs?
   Where is the roofline, and where did extra threads stop paying?
8. **Trade-offs.** What did you deliberately *not* do (intrinsics, cache blocking,
   hardcoded threads) and why?
9. **Generated code.** Did you read a vectorisation report or the assembly? If a
   loop did not vectorise, why not?
10. **Failure modes.** What happens for `1×N`, `N×1`, `2×2`, `rows < 3`? For the
    same grid passed twice?

---

## 19. Risks and open questions

| Risk | Mitigation |
| --- | --- |
| Local build lacks OpenMP → serial pragmas, misleading scaling | Install/build with OpenMP; document thread-count source. |
| Local flags differ from evaluator (`-march`) | Test with `-DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=x86-64-v3"` in a scratch build. |
| Hidden non-square / tiny cases | Independent local test matrix over shapes. |
| `restrict` violated if grids alias | Documented precondition + debug assert; harness ping-pongs. |
| Undefined behaviour hidden by `-O3` | ASan/UBSan build (and TSan for the parallel path); see [`PERFORMANCE.md`](./PERFORMANCE.md). |
| Runs vary run-to-run | Compare distributions; change one variable at a time. |
| Oversubscription on shared evaluator hardware | Do not hardcode thread counts; respect runtime defaults. |

---

## 20. Design-to-rubric map

| Evaluation criterion | Where the design addresses it |
| --- | --- |
| Correctness | Guarded loops, verbatim boundary, independent test matrix. |
| Implementation quality | RAII, rule of zero, `#pragma once`, `inline`, no dead code. |
| Design decisions | D1–D12, each with rationale and rejected alternatives. |
| Performance | Flat/padded layout, SIMD-friendly loop, OpenMP rows, roofline-aware. |
| Explainability | Decision log + review talking points; why-comments in code. |
