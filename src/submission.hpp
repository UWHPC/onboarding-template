#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <immintrin.h>

// ─── Constants & Types ─────────────────────────────────────────────────────

// The top performing PRs pad the left side of every row with 3 doubles.
// Since each row's allocation is 64-byte aligned, skipping 3 doubles (24 bytes)
// means column 0 is offset by 24 bytes.
// Column 1 is offset by 24 + 8 = 32 bytes from a 64-byte boundary.
// This perfectly 32-byte aligns column 1, allowing aligned loads/stores (_mm256_load_pd)
// for the interior of the stencil, maximizing AVX2 performance without loop peeling.
constexpr std::size_t PREFIX_PADDING = 3;

struct AlignedDeleter {
  void operator()(double* p) const noexcept {
    std::free(p);
  }
};
using AlignedBuffer = std::unique_ptr<double[], AlignedDeleter>;

inline AlignedBuffer make_aligned_buffer(std::size_t n) {
  auto* p = static_cast<double*>(std::aligned_alloc(64, n * sizeof(double)));
  std::memset(p, 0, n * sizeof(double));
  return AlignedBuffer(p);
}

// ─── Non-owning grid views ─────────────────────────────────────────────────

template <typename T>
struct GridSpan {
  T*          ptr;
  std::size_t rows;
  std::size_t cols;
  std::size_t stride;
  bool        boundary_formed;

  T* row(std::size_t i) const noexcept {
    return static_cast<T*>(
        __builtin_assume_aligned(ptr + i * stride - PREFIX_PADDING, 64)) + PREFIX_PADDING;
  }
};

using ConstGridSpan   = GridSpan<const double>;
using MutableGridSpan = GridSpan<double>;

// ─── Grid (owning type) ───────────────────────────────────────────────────

class Grid {
private:
  std::size_t   rows_;
  std::size_t   cols_;
  std::size_t   stride_;
  AlignedBuffer store_;
  bool          boundary_formed_;

public:
  Grid(std::size_t rows, std::size_t cols)
    : rows_(rows)
    , cols_(cols)
    // Add padding to stride and round up to a multiple of 8 (64 bytes)
    , stride_((PREFIX_PADDING + cols + 7) & ~std::size_t(7))
    , store_(make_aligned_buffer(rows_ * stride_))
    , boundary_formed_(false)
  {}

  double& operator()(std::size_t i, std::size_t j)       { return store_[i * stride_ + PREFIX_PADDING + j]; }
  double  operator()(std::size_t i, std::size_t j) const { return store_[i * stride_ + PREFIX_PADDING + j]; }
  std::size_t rows()   const { return rows_; }
  std::size_t cols()   const { return cols_; }
  std::size_t stride() const { return stride_; }
  const double* data() const { return store_.get() + PREFIX_PADDING; }
  double*       data()       { return store_.get() + PREFIX_PADDING; }

  void set_boundary_formed() { boundary_formed_ = true; }
  bool boundary_formed() const { return boundary_formed_; }

  ConstGridSpan   const_span()   const { return {store_.get() + PREFIX_PADDING, rows_, cols_, stride_, boundary_formed_}; }
  MutableGridSpan mutable_span()       { return {store_.get() + PREFIX_PADDING, rows_, cols_, stride_, boundary_formed_}; }
};

// ─── Boundary copy ─────────────────────────────────────────────────────────

inline void copy_top_bottom(ConstGridSpan in, MutableGridSpan out) noexcept {
  if (in.boundary_formed) return;

  std::memcpy(out.ptr, in.ptr, in.cols * sizeof(double));
  std::memcpy(out.row(in.rows - 1), in.row(in.rows - 1),
              in.cols * sizeof(double));
}

// ─── Interior kernel (Explicit AVX2) ───────────────────────────────────────

inline __attribute__((target("avx2,fma"))) void stencil_row(const double* __restrict__ prev,
                        const double* __restrict__ curr,
                        const double* __restrict__ next,
                        double*       __restrict__ dst,
                        std::size_t cols) noexcept {
  std::size_t j = 1;
  
  __m256d c0 = _mm256_set1_pd(0.5);
  __m256d c1 = _mm256_set1_pd(0.125);

  // AVX2 block: process 4 elements per iteration
  for (; j + 3 < cols - 1; j += 4) {
    // Aligned loads for column j because of PREFIX_PADDING
    __m256d p = _mm256_load_pd(&prev[j]);
    __m256d c = _mm256_load_pd(&curr[j]);
    __m256d n = _mm256_load_pd(&next[j]);
    
    // Unaligned loads for j-1 and j+1
    __m256d l = _mm256_loadu_pd(&curr[j - 1]);
    __m256d r = _mm256_loadu_pd(&curr[j + 1]);
    
    __m256d sum_neighbors = _mm256_add_pd(_mm256_add_pd(p, n), _mm256_add_pd(l, r));
    __m256d res = _mm256_fmadd_pd(c1, sum_neighbors, _mm256_mul_pd(c0, c));
    
    _mm256_store_pd(&dst[j], res);
  }
  
  // Epilogue for the remaining elements
  for (; j < cols - 1; ++j) {
    dst[j] = 0.5 * curr[j] + 0.125 * (prev[j] + next[j] + curr[j - 1] + curr[j + 1]);
  }
}

// ─── apply_stencil (parallel orchestration) ────────────────────────────────

void apply_stencil(Grid& old_grid, Grid& new_grid) {
  ConstGridSpan   in  = old_grid.const_span();
  MutableGridSpan out = new_grid.mutable_span();

  // Copy top and bottom boundaries (skipped after first iteration)
  copy_top_bottom(in, out);

  const std::size_t rows = in.rows;
  const std::size_t cols = in.cols;

  // Static scheduling for thread locality. Left and right boundaries are fused
  // directly into this loop so threads populate their own border cells.
  #pragma omp parallel for schedule(static)
  for (std::size_t i = 1; i < rows - 1; ++i) {
    const double* prev = in.row(i - 1);
    const double* curr = in.row(i);
    const double* next = in.row(i + 1);
    double* dst = out.row(i);
    
    // Fuse left and right boundary copies
    if (!in.boundary_formed) {
      dst[0] = curr[0];
      dst[cols - 1] = curr[cols - 1];
    }
    
    stencil_row(prev, curr, next, dst, cols);
  }
  
  old_grid.set_boundary_formed();
  new_grid.set_boundary_formed();
}
