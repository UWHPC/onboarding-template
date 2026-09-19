#pragma once

// 2D heat-diffusion submission.
//
// Design decisions D1-D12 from DESIGN.md are honoured here: one flat, padded,
// 64-byte-aligned RAII buffer (D1/D2/D3), rule-of-zero value semantics (D4),
// non-owning views carrying pointer + shape together (D5/D11), a documented
// no-alias contract with `__restrict__` used only where it is true (D6), a
// verbatim boundary copy folded into a single parallel row loop (D7/D8), a
// SIMD-friendly inner loop the compiler can vectorise (D9), and OpenMP over
// output rows with a guarded `proc_bind(spread)` clause that only takes effect
// when the environment configures places (D10). The file is header-only and
// ODR-safe (D12).

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace detail {

// Cache-line / SIMD alignment. 64 bytes == 8 doubles.
inline constexpr std::size_t kAlignment = 64;
inline constexpr std::size_t kSimdWidth = kAlignment / sizeof(double);

// Padded row pitch: rounded up to a whole number of vector lanes so every row
// starts at the same alignment and adjacent rows never share a cache line
// (D2). Throws rather than wrapping for a `cols` so large the round-up would
// overflow, which keeps absurd shapes from silently under-allocating.
inline std::size_t padded_stride(std::size_t cols) {
  if (cols > std::numeric_limits<std::size_t>::max() - (kSimdWidth - 1)) {
    throw std::bad_alloc{};
  }
  return ((cols + kSimdWidth - 1) / kSimdWidth) * kSimdWidth;
}

// 64-byte-aligned allocator giving the owning container real alignment at
// allocation time; align attributes/pragmas cannot fix an unaligned base (D2).
template <class T, std::size_t Align>
struct AlignedAllocator {
  using value_type = T;

  AlignedAllocator() noexcept = default;
  template <class U>
  AlignedAllocator(const AlignedAllocator<U, Align>&) noexcept {}

  [[nodiscard]] T* allocate(std::size_t n) {
    if (n == 0) {
      return nullptr;
    }
    return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{Align}));
  }

  void deallocate(T* p, std::size_t) noexcept {
    ::operator delete(p, std::align_val_t{Align});
  }

  template <class U>
  struct rebind {
    using other = AlignedAllocator<U, Align>;
  };

  friend bool operator==(const AlignedAllocator&, const AlignedAllocator&) noexcept {
    return true;
  }
  friend bool operator!=(const AlignedAllocator&, const AlignedAllocator&) noexcept {
    return false;
  }
};

// Disjointness check backing the debug-only no-alias assertion (D6). Values
// are converted through uintptr_t so empty buffers (null data) are handled
// without pointer arithmetic.
inline bool buffers_overlap(
  const double* a, std::size_t a_count,
  const double* b, std::size_t b_count
) noexcept {
  if (a_count == 0 || b_count == 0) {
    return false;
  }
  const std::uintptr_t a_begin = reinterpret_cast<std::uintptr_t>(a);
  const std::uintptr_t b_begin = reinterpret_cast<std::uintptr_t>(b);
  const std::uintptr_t a_end = a_begin + a_count * sizeof(double);
  const std::uintptr_t b_end = b_begin + b_count * sizeof(double);
  return a_begin < b_end && b_begin < a_end;
}

}  // namespace detail

// Read-only, non-owning view: pointer + shape travel together so dimensions and
// stride cannot be transposed by accident (D5/D11). Construction is a handful
// of register moves and never allocates.
class ConstGridView {
private:
  const double* data_{nullptr};
  std::size_t rows_{0};
  std::size_t cols_{0};
  std::size_t stride_{0};

public:
  ConstGridView() noexcept = default;

  ConstGridView(
    const double* data, std::size_t rows, std::size_t cols, std::size_t stride
  ) noexcept
    : data_{data}, rows_{rows}, cols_{cols}, stride_{stride} {}

  double operator()(std::size_t i, std::size_t j) const noexcept {
    return data_[i * stride_ + j];
  }

  const double* row(std::size_t i) const noexcept {
    return data_ + i * stride_;
  }

  const double* base() const noexcept { return data_; }

  std::size_t rows() const noexcept { return rows_; }
  std::size_t cols() const noexcept { return cols_; }
  std::size_t stride() const noexcept { return stride_; }
};

// Mutable counterpart of ConstGridView. The mutable/read-only distinction is a
// type, so "which side is writable" is checked by the compiler (D5).
class GridView {
private:
  double* data_{nullptr};
  std::size_t rows_{0};
  std::size_t cols_{0};
  std::size_t stride_{0};

public:
  GridView() noexcept = default;

  GridView(
    double* data, std::size_t rows, std::size_t cols, std::size_t stride
  ) noexcept
    : data_{data}, rows_{rows}, cols_{cols}, stride_{stride} {}

  double& operator()(std::size_t i, std::size_t j) const noexcept {
    return data_[i * stride_ + j];
  }

  double* row(std::size_t i) const noexcept {
    return data_ + i * stride_;
  }

  double* base() const noexcept { return data_; }

  std::size_t rows() const noexcept { return rows_; }
  std::size_t cols() const noexcept { return cols_; }
  std::size_t stride() const noexcept { return stride_; }
};

// Owns exactly one flat, row-major, padded, 64-byte-aligned allocation of
// rows x stride doubles (D1/D2/D3). The declared interface -- constructor and
// both operator() overloads -- is preserved exactly.
class Grid {
private:
  std::size_t rows_{0};
  std::size_t cols_{0};
  std::size_t stride_{0};
  std::vector<double, detail::AlignedAllocator<double, detail::kAlignment>> data_;

public:
  Grid(std::size_t rows, std::size_t cols)
    : rows_{rows},
      cols_{cols},
      stride_{cols == 0 ? 0 : detail::padded_stride(cols)} {
    // Reject shapes whose padded element count cannot be represented or
    // allocated before any multiplication can overflow.
    if (rows_ != 0 && stride_ != 0) {
      if (rows_ > data_.max_size() / stride_) {
        throw std::bad_alloc{};
      }
      // Zero-initialise (G2) through the RAII container (D3).
      data_.assign(rows_ * stride_, 0.0);
    }
  }

  double& operator()(std::size_t i, std::size_t j) {
    assert(i < rows_ && j < cols_);
    return data_[i * stride_ + j];
  }

  double operator()(std::size_t i, std::size_t j) const {
    assert(i < rows_ && j < cols_);
    return data_[i * stride_ + j];
  }

  // Dimensions are exposed so the stencil can interpret the buffer without
  // reaching into storage internals (G6). Raw storage stays private; callers
  // go through the views, which keep shape and pointer together (D5).
  std::size_t rows() const noexcept { return rows_; }
  std::size_t cols() const noexcept { return cols_; }
  std::size_t stride() const noexcept { return stride_; }

  ConstGridView const_view() const noexcept {
    return ConstGridView{data_.data(), rows_, cols_, stride_};
  }

  GridView view() noexcept {
    return GridView{data_.data(), rows_, cols_, stride_};
  }
};

// OpenMP 4.0 (201307) added the `proc_bind` clause; an older or absent OpenMP
// must not see it, so the clause text is selected at compile time and expanded
// to nothing otherwise. `proc_bind` sets *placement* only -- which CPUs the
// team occupies -- never thread *count*, so it stays inside D10's split: the
// runtime still chooses how many threads to use.
#if defined(_OPENMP) && _OPENMP >= 201307
#  define UWHPC_OMP_PROC_BIND_SPREAD proc_bind(spread)
#else
#  define UWHPC_OMP_PROC_BIND_SPREAD
#endif

// Apply the five-point stencil to every interior point and copy the boundary
// verbatim (S1/S2/S5). `old_grid` is read-only (S3); the result is computed
// solely from it (S4).
inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const ConstGridView in{old_grid.const_view()};
  const GridView out{new_grid.view()};

  // Precondition (D6): distinct grids with disjoint storage, and both must
  // describe the same shape (the kernel indexes `out` while reading `in`). The
  // harness ping-pongs two separately constructed Grid objects, so this holds.
  // The `__restrict__` row pointers below are valid *only* because of it; if the
  // buffers aliased the promise would be false and the behaviour undefined.
  assert(&old_grid != &new_grid);
  assert(in.rows() == out.rows() && in.cols() == out.cols());
  assert(!detail::buffers_overlap(
    in.base(), in.rows() * in.stride(),
    out.base(), out.rows() * out.stride()
  ));

  const std::size_t rows = in.rows();
  const std::size_t cols = in.cols();

  if (rows == 0 || cols == 0) {
    return;
  }

  // One parallel region per call (D7/D10). Each output row is written by
  // exactly one worker, so boundary work cannot race with interior work.
  // `i + 1 == rows` (not `i < rows - 1`) keeps tiny grids free of unsigned
  // underflow.
  //
  // `proc_bind(spread)` biases placement: it asks the runtime to spread the
  // team over distinct cores/sockets instead of packing and freely migrating
  // it. The clause does not create binding on its own (scope limit below), so
  // the shared-host score modes are an environment property, not a clause
  // effect: the same (pre-clause) binary measured ~0.99 at 16 threads fully
  // unbound vs ~3.10 with `OMP_PLACES=cores OMP_PROC_BIND=spread` (log
  // [0008] A/B). Scope limit: on
  // libgomp this clause takes effect only when the environment configures
  // places (`OMP_PLACES`/`OMP_PROC_BIND`); otherwise placement stays unbound,
  // so the clause is defensive rather than a fix for the unbound default.
  // Where places exist it overrides any `OMP_PROC_BIND` the user exported for
  // this region -- the accepted cost of that placement choice. Placement only:
  // thread count is still the runtime's (D10), and no numerical path changes.
  #pragma omp parallel for schedule(static) UWHPC_OMP_PROC_BIND_SPREAD
  for (std::size_t i = 0; i < rows; ++i) {
    double* __restrict__ out_row = out.row(i);

    if (i == 0 || i + 1 == rows) {
      // Top/bottom rows are contiguous: copy exactly the logical columns.
      // Padding is storage only and must never reach a logical cell (D2/D7).
      std::memcpy(out_row, in.row(i), cols * sizeof(double));
      continue;
    }

    const double* __restrict__ up = in.row(i - 1);
    const double* __restrict__ mid = in.row(i);
    const double* __restrict__ down = in.row(i + 1);

    // Left/right boundary cells of an interior row, copied verbatim (D7).
    // For cols == 1 the two writes target the same cell and agree.
    out_row[0] = mid[0];
    out_row[cols - 1] = mid[cols - 1];

    // Interior columns: contiguous, row bases hoisted, no loop-carried
    // dependence. The `__restrict__` qualifiers make the no-alias contract
    // visible so the loop can vectorise (D6/D8/D9). The canonical bound
    // `j < last` is required by `omp simd`; the scalar remainder is inherent in
    // the odd interior width.
    const std::size_t last = cols - 1;  // cols >= 1 here, so no underflow
    #pragma omp simd
    for (std::size_t j = 1; j < last; ++j) {
      out_row[j] = 0.5 * mid[j] +
                   0.125 * (up[j] + down[j] + mid[j - 1] + mid[j + 1]);
    }
  }
}

// Keep the helper macro out of any translation unit that includes this header.
#undef UWHPC_OMP_PROC_BIND_SPREAD
