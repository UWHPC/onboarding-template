# Requirements — UWHPC Onboarding: 2D Heat Diffusion

> **Purpose.** This document is the normative specification of *what* the submission
> must do. It consolidates the public onboarding material and the supplied harness
> into a single checkable list. For *how* the solution is built, see
> [`DESIGN.md`](./DESIGN.md); for *how performance is pursued and measured*, see
> [`PERFORMANCE.md`](./PERFORMANCE.md).
>
> Upstream sources: [Onboarding](https://docs.uwhpc.com/onboarding/),
> [Problem statement](https://docs.uwhpc.com/onboarding/problem-statement/),
> [Performance design](https://docs.uwhpc.com/onboarding/performance-design/),
> [Complete the exercise](https://docs.uwhpc.com/onboarding/complete-the-exercise/).

---

## 1. Purpose and scope

Implement two focused components of a 2D heat-diffusion solver:

1. A `Grid` class that stores a 2D field of `double` values.
2. `apply_stencil`, which advances the interior of the field by one time step.

Everything else — time integration, boundary setup, initial conditions, I/O,
correctness testing, benchmarking, and the build — is supplied and must not be
modified. No thermal-physics knowledge is required.

Both components live in a single file, `src/submission.hpp`, which ships with the
interface declared and no implementations. Until both are implemented the project
does not link.

---

## 2. Deliverables and submission mechanics

| Item | Requirement |
| --- | --- |
| Edited file | `src/submission.hpp` **only**. |
| Language | C++17 (the standard library is fully available). |
| Deliverable | A pull request from your fork against `UWHPC/onboarding-template`. |
| PR state | The evaluator converts it to **draft** automatically. Do not merge it. Leave it open as the submission record. |
| Iteration | Push further commits; the sticky result comment updates in place. |
| Communication | Post your name + PR link in the **onboarding** forum channel on the UWHPC Discord. |
| Follow-up | A short virtual chat defending your design choices. |

Submissions are judged holistically: **correctness, implementation quality, design
decisions, and performance**. Correctness is necessary but not sufficient. Passing
the score threshold is a floor, not a target.

---

## 3. Domain model

### 3.1 Governing equation (context only)

```
∂T/∂t = α ( ∂²T/∂x² + ∂²T/∂y² )
```

`T(x, y, t)` is temperature and `α` is a constant. This is background; the
discretisation below is what must be implemented.

### 3.2 Discretisation

With `α`, `Δx`, `Δy`, `Δt` chosen for stability, the update reduces to a weighted
average of a cell and its four axis-aligned neighbours:

```
T_{t+1}(i, j) = 0.5   * T_t(i, j)
              + 0.125 * ( T_t(i-1, j) + T_t(i+1, j)
                        + T_t(i, j-1) + T_t(i, j+1) )
```

### 3.3 The five-point stencil

```
                     (i-1, j)          ×0.125   (up)
( i, j-1 ) ×0.125     (i,  j) ×0.5     ( i, j+1 ) ×0.125
                     (i+1, j)          ×0.125   (down)
```

The centre cell is weighted `0.5`; each of the four neighbours is weighted
`0.125`. The weights sum to `1.0`, so a uniform field is unchanged (a useful
invariant for self-testing).

### 3.4 Indexing conventions (normative)

- `i` selects the **row**, in `[0, rows)`. `j` selects the **column**, in `[0, cols)`.
- `rows` and `cols` are **independent**; rows and columns are *not* interchangeable.
- The **interior** is `1 ≤ i < rows - 1` and `1 ≤ j < cols - 1`.
- The **boundary** is the outermost row and column on each side: `i == 0`,
  `i == rows - 1`, `j == 0`, `j == cols - 1`.
- `operator()(i, j)` addresses row `i`, column `j`.

> The non-square public cases exist specifically to catch a layout that conflates
> rows and columns, or that confuses the logical width of a row (`cols`) with the
> physical distance between row starts (the stride).

---

## 4. Task 1 — The `Grid` class

### 4.1 Required interface (must be preserved exactly)

```cpp
#pragma once

#include <cstddef>

class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;

public:
  Grid(std::size_t rows, std::size_t cols);

  double& operator()(std::size_t i, std::size_t j);
  double  operator()(std::size_t i, std::size_t j) const;
};
```

The harness uses **only** these two `operator()` overloads to set initial conditions
and to read results. The overloads must work regardless of how the field is stored
internally.

### 4.2 Functional requirements

| ID | Requirement |
| --- | --- |
| G1 | The constructed grid has `rows` rows and `cols` columns. |
| G2 | The field is **default-initialised to zero**. |
| G3 | Non-const `operator()(i, j)` returns a mutable reference to the cell. |
| G4 | Const `operator()(i, j)` returns the cell value by copy (read-only). |
| G5 | `operator()` must address the cell at row `i`, column `j` for every `0 ≤ i < rows`, `0 ≤ j < cols`. |

### 4.3 Degrees of freedom

The problem statement explicitly grants freedom over:

- **Memory layout** (flat vs. nested, row-major vs. column-major, padding, alignment).
- **Internal data structures** (owning buffer, allocator, views, helpers).
- **Additional members and methods**, including size accessors.

### 4.4 Implicit contracts

| ID | Contract |
| --- | --- |
| G6 | The harness never asks `Grid` for its dimensions, so no size accessors are *prescribed* — but the stencil still needs the dimensions, so exposing them is part of the design. |
| G7 | `Grid` is constructed **by value** and used through references; it must be safe under whatever copy/move semantics you choose. |
| G8 | The harness only ever calls `operator()` with in-range indices; out-of-range behaviour is unspecified but should not be undefined in normal use. |

---

## 5. Task 2 — The stencil kernel

### 5.1 Declaration

```cpp
void apply_stencil(const Grid& old_grid, Grid& new_grid);
```

### 5.2 Behavioural requirements

On return, `new_grid` must hold a **complete field**:

| ID | Requirement |
| --- | --- |
| S1 | Every **interior** point `(i, j)` is the weighted average above, computed entirely from `old_grid`. |
| S2 | Every **boundary** point is copied from `old_grid` **unchanged**. |
| S3 | `old_grid` must **not** be modified. |
| S4 | `new_grid` must not read from its own pre-call contents for the result (only `old_grid` is input). |
| S5 | No boundary conditions need to be implemented; the boundary is a verbatim copy. |

### 5.3 Boundary contract in full

Boundary rows and columns are copied exactly:

```
for each row i:            new_grid(i, 0)        = old_grid(i, 0)
                           new_grid(i, cols-1)   = old_grid(i, cols-1)
for each column j:         new_grid(0, j)        = old_grid(0, j)
                           new_grid(rows-1, j)   = old_grid(rows-1, j)
```

Boundary values never change across time steps (they are repeatedly copied from
the previous field, whose boundary equals the initial boundary). This is an
invariant you can exploit for verification.

### 5.4 Degenerate shapes

The kernel is defined for all `rows, cols ≥ 1`:

| Shape | Expected behaviour |
| --- | --- |
| `rows < 3` or `cols < 3` | No interior points; every cell is boundary and is copied verbatim. |
| `rows == 1` / `cols == 1` | A single row/column; all boundary, copied verbatim. |
| `rows == rows-1` overlap | Top and bottom rows may coincide; copying either is correct. |

Use loop conditions such as `for (i = 1; i + 1 < rows; ++i)` rather than
`i < rows - 1` so that unsigned subtraction never wraps for tiny grids.

---

## 6. Toolchain and build requirements

| Item | Requirement |
| --- | --- |
| Standard | **C++17** (`CMAKE_CXX_STANDARD 17`, required). |
| CMake | ≥ 3.21. |
| Generator | Ninja. |
| Build type | `Release` with `-O3` (preset `benchmark`). |
| OpenMP | Optional locally; **enabled on the official evaluator**. |
| Evaluator arch flags | `-march=x86-64-v3` on x86 (AVX2/FMA baseline). |
| Preset | `cmake --preset benchmark` produces `build/benchmark`. |

Comment in `CMakeLists.txt`:

> The official evaluator builds with OpenMP enabled and `-march=x86-64-v3` on x86,
> so `#pragma omp` code runs multithreaded there even if your local build is serial.

> **Consequence:** a local build that did not find OpenMP compiles the same pragmas
> serially, making any local scaling conclusion meaningless. Install `libomp`
> (or use a compiler with built-in OpenMP) before drawing parallel conclusions.

---

## 7. Correctness verification

### 7.1 Public cases (`--check`)

The harness steps the submission and an independent reference implementation in
lockstep and compares the two final fields cell by cell.

| Case | Grid `(rows × cols)` | Steps | Initial condition |
| --- | --- | --- | --- |
| `public/square-32` | 32 × 32 | 20 | Center block |
| `public/nonsquare-48x80` | 48 × 80 | 40 | Linear gradient |
| `public/checker-64` | 64 × 64 | 30 | Checkerboard |
| `public/one-step-50` | 50 × 50 | 1 | Center block |
| `public/nonsquare-boundary-80x50` | 80 × 50 | 10 | Linear gradient |

> The repository `README.md` says "four public correctness cases"; the harness and
> the onboarding docs specify **five**. Trust the harness.

### 7.2 Tolerance

A case passes when the largest absolute difference from the reference is
`≤ 1e-6`. Differences of this size are far above double rounding error, so
reasonable reordering/FMA contraction is safe; `float` storage is not.

### 7.3 Hidden cases

The evaluator runs the public cases **plus private cases you cannot see**. A local
pass is necessary but not sufficient. A failing comment names the *category* of
failure (e.g. "non-square grids") but never the exact hidden case.

### 7.4 Edge cases you should self-test

- Both non-square orientations (`rows > cols` and `cols > rows`).
- Degenerate shapes: `1×1`, `1×N`, `N×1`, `2×2`, `3×3`.
- `rows < 3` or `cols < 3` (no interior).
- Large grids (memory and indexing overflow).
- One step vs. many steps.
- All three initial conditions.
- Uniform field (must remain uniform).
- Checkerboard (maximum neighbour contrast).

---

## 8. Benchmark and scoring

### 8.1 Benchmark shape

- Grid: **1024 × 1024**.
- Time steps: **200**.
- Initial condition: center block.
- The timing loop calls `apply_stencil` 200 times with ping-ponged buffers.

### 8.2 Output

One line of JSON to stdout:

```json
{ "runtime_ms": 148.404, "memory_mb": 16.777, "score": 1.293 }
```

- `runtime_ms` — wall-clock time of the 200 submission steps.
- `memory_mb` — reported as `2 · rows · cols · 8 / 1e6` (decimal MB); it is a fixed
  function of the grid size, **not** a measurement of your allocation. It does not
  reward or punish your storage choices.
- `score` — `reference_ms / submission_ms`; **higher is faster**.

### 8.3 Score and threshold

- The reference is deliberately naive and is a **floor** to clear, not a design to
  imitate.
- `score = 1.0` ties the reference; `> 1.0` beats it.
- **The submission must score over `0.9` to pass.** The evaluator marks anything
  at or below `0.9` as a failing check ("within 90 % of the reference baseline").
- Local results vary by machine; the submitted result is measured on team
  infrastructure.
- The evaluator may run the benchmark multiple times; per-run timings are exposed
  for diagnosing variance.

---

## 9. Acceptance criteria

A submission is accepted for review when **all** of the following hold:

- [ ] `src/submission.hpp` is the only file changed.
- [ ] The project builds cleanly with the `benchmark` preset (the evaluator uses
      `-O3`, OpenMP, and `-march=x86-64-v3`).
- [ ] `ctest --preset benchmark` passes all five public cases.
- [ ] The evaluator's hidden cases pass.
- [ ] The benchmark score is **> 0.9**.
- [ ] `old_grid` is not modified; boundaries are copied; interiors use the exact
      weighted average.
- [ ] No undefined behaviour: no out-of-bounds access, no invalid aliasing
      assumptions, no leak/double-free, no data races.
- [ ] The code is the applicant's own work and can be defended in the chat.

---

## 10. Edit-scope constraints

| Path | May edit? | Why |
| --- | --- | --- |
| `src/submission.hpp` | **Yes** | The only permitted edit. |
| `bench/main.cpp` | No | Evaluation harness; fixed interfaces and behaviour. |
| `CMakeLists.txt` | No | Evaluator build configuration. |
| `CMakePresets.json` | No | Evaluator presets. |
| `.github/workflows/uwhpc-evaluate.yml` | No | Evaluation pipeline. |

Everything the submission needs must therefore live in a header:
`#pragma once`, no external linkage that breaks when many translation units
include it, no leftover experiments or debug output.

---

## 11. Requirement traceability

| Requirement | Where addressed | How verified |
| --- | --- | --- |
| G1–G5 grid shape, zero-init, access | `Grid` ctor + `operator()` | harness `--check`; unit self-test |
| G6 dimensions available to kernel | size/stride accessors + view | kernel compiles and runs |
| G7 value semantics safe | rule-of-five / rule-of-zero | review + move/copy test |
| S1 interior update | interior kernel | `--check` all cases |
| S2 boundary copy | boundary routine | `--check`, uniform-field test |
| S3 `old_grid` const | `const Grid&` + `const` views | compiler + review |
| S4 no self-read | reads only from `old` | `--check` |
| S5 no boundary conditions | verbatim copy | `--check` |
| Degenerate shapes | guarded loops | extra local cases |
| Performance > 0.9 | layout + SIMD + OpenMP | benchmark JSON |
| Scope | one-file change | `git diff --stat` |

---

## 12. Glossary

| Term | Meaning |
| --- | --- |
| **Interior** | Cells with `1 ≤ i < rows-1` and `1 ≤ j < cols-1`. |
| **Boundary** | The outermost row/column ring; copied verbatim. |
| **Stride / pitch** | Number of stored `double`s between the starts of adjacent rows; may exceed `cols` when padded. |
| **Padding** | Extra storage elements past `cols` that keep rows aligned; never logically addressable. |
| **Ping-pong** | Two buffers alternate old/new roles each step so the update can read `old` while writing `new`. |
| **View** | A small non-owning value type holding a pointer plus dimensions and stride. |
| **Ping-pong call** | One invocation of `apply_stencil`; the benchmark makes 200. |
| **Score** | `reference_ms / submission_ms`; `> 0.9` required. |
