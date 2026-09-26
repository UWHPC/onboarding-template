// Harness needs Grid(rows, cols), operator()(i, j) and apply_stencil. The rest is internal.

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <vector>

struct Extent {
  std::size_t rows;
  std::size_t cols;
};

template <typename T>
struct GridView {
  T* cells;
  Extent extent;
  std::size_t stride;

  T* row(std::size_t i) const { return cells + i * stride; }
};

class Grid {
private:
  // x86-64 cache line
  static constexpr std::size_t kCacheLineBytes{64};
  static constexpr std::size_t kDoublesPerCacheLine{kCacheLineBytes / sizeof(double)};

  Extent extent_;
  std::size_t stride_;
  std::vector<double> cells_;

public:
  Grid(std::size_t rows, std::size_t cols)
    : extent_{rows, cols}
    // whole-line rows, so every row has the same alignment
    , stride_{(cols + kDoublesPerCacheLine - 1) / kDoublesPerCacheLine * kDoublesPerCacheLine}
    , cells_(rows * stride_, 0.0)
  {
    // the round-up and rows * stride_ both wrap near SIZE_MAX
    if (stride_ < cols || (rows != 0 && cells_.size() / rows != stride_)) {
      throw std::length_error("grid extent overflows size_t");
    }
  }

  double& operator()(std::size_t i, std::size_t j) {
    assert(i < extent_.rows && j < extent_.cols);
    return cells_[i * stride_ + j];
  }

  double  operator()(std::size_t i, std::size_t j) const {
    assert(i < extent_.rows && j < extent_.cols);
    return cells_[i * stride_ + j];
  }

  GridView<const double> view() const { return {cells_.data(), extent_, stride_}; }
  GridView<double>       view()       { return {cells_.data(), extent_, stride_}; }
};

// out must not overlap the inputs
inline void apply_stencil_row(
  const double* __restrict__ above, const double* __restrict__ center,
  const double* __restrict__ below, double* __restrict__ out, std::size_t cols
) {
  out[0] = center[0];
  out[cols - 1] = center[cols - 1];

  // omp needs j = 1 here, g++ rejects j{1}
  #pragma omp simd
  for (std::size_t j = 1; j < cols - 1; ++j) {
    out[j] = 0.5 * center[j] + 0.125 * (above[j] + below[j] + center[j - 1] + center[j + 1]);
  }
}

inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const GridView<const double> old_view{old_grid.view()};
  const GridView<double> new_view{new_grid.view()};
  const Extent extent{old_view.extent};

  assert(extent.rows == new_view.extent.rows && extent.cols == new_view.extent.cols);

  if (extent.rows == 0 || extent.cols == 0) {
    return;
  }

  // top and bottom are copied, left and right happen per row in the kernel
  std::copy_n(old_view.row(0), extent.cols, new_view.row(0));
  std::copy_n(old_view.row(extent.rows - 1), extent.cols, new_view.row(extent.rows - 1));

  #pragma omp parallel for schedule(static)
  for (std::size_t i = 1; i < extent.rows - 1; ++i) {
    apply_stencil_row(
      old_view.row(i - 1), old_view.row(i), old_view.row(i + 1), new_view.row(i), extent.cols
    );
  }
}
