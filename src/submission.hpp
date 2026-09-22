#pragma once

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// Views provide access to Grid storage without allocating or copying elements.
template <typename Element>
struct BasicGridView {
  Element* values;
  std::size_t rows;
  std::size_t cols;
  std::size_t stride;

  Element* row_data(const std::size_t row) const noexcept {
    return values + row * stride;
  }

  Element& operator()(const std::size_t row, const std::size_t col) const noexcept {
    return row_data(row)[col];
  }
};

using ConstGridView = BasicGridView<const double>;
using GridView = BasicGridView<double>;

// Grid owns one contiguous row-major allocation and its lifetime.
class Grid {
private:
  static constexpr std::size_t cache_line_elements{64 / sizeof(double)};

  std::size_t rows_;
  std::size_t cols_;
  // One extra cache line rotates adjacent rows through cache sets.
  std::size_t stride_;
  std::vector<double> values_;

  static std::size_t padded_stride(const std::size_t cols) {
    if (cols == 0) {
      return 0;
    }

    constexpr std::size_t maximum_padding{2 * cache_line_elements - 1};
    if (cols > std::numeric_limits<std::size_t>::max() - maximum_padding) {
      throw std::length_error(
          "Grid column count is too large for cache-line stride padding");
    }

    const std::size_t rounded_cols{
        (cols + cache_line_elements - 1) / cache_line_elements *
        cache_line_elements};
    return rounded_cols + cache_line_elements;
  }

  static std::size_t checked_element_count(const std::size_t rows,
                                           const std::size_t stride) {
    if (stride != 0 && rows > std::numeric_limits<std::size_t>::max() / stride) {
      throw std::length_error("Grid dimensions are too large for one allocation");
    }

    return rows * stride;
  }

  std::size_t checked_offset(const std::size_t row, const std::size_t col) const {
    if (row >= rows_ || col >= cols_) {
      throw std::out_of_range("Grid index (" + std::to_string(row) + ", " +
                              std::to_string(col) + ") is outside dimensions (" +
                              std::to_string(rows_) + ", " + std::to_string(cols_) +
                              ")");
    }

    return row * stride_ + col;
  }

public:
  Grid(const std::size_t rows, const std::size_t cols)
      : rows_{rows}, cols_{cols}, stride_{padded_stride(cols)},
        values_(checked_element_count(rows, stride_), 0.0) {
  }

  std::size_t rows() const noexcept {
    return rows_;
  }

  std::size_t cols() const noexcept {
    return cols_;
  }

  ConstGridView read_view() const noexcept {
    return ConstGridView{values_.data(), rows_, cols_, stride_};
  }

  GridView write_view() noexcept {
    return GridView{values_.data(), rows_, cols_, stride_};
  }

  double& operator()(const std::size_t row, const std::size_t col) {
    return values_[checked_offset(row, col)];
  }

  double operator()(const std::size_t row, const std::size_t col) const {
    return values_[checked_offset(row, col)];
  }
};

namespace uwhpc_detail {

inline void copy_boundaries(const ConstGridView old_grid, const GridView new_grid) {
  if (old_grid.rows == 0 || old_grid.cols == 0) {
    return;
  }

  for (std::size_t col{0}; col < old_grid.cols; ++col) {
    new_grid(0, col) = old_grid(0, col);

    if (old_grid.rows > 1) {
      new_grid(old_grid.rows - 1, col) = old_grid(old_grid.rows - 1, col);
    }
  }

  for (std::size_t row{1}; row + 1 < old_grid.rows; ++row) {
    new_grid(row, 0) = old_grid(row, 0);

    if (old_grid.cols > 1) {
      new_grid(row, old_grid.cols - 1) = old_grid(row, old_grid.cols - 1);
    }
  }
}

inline void update_interior(const ConstGridView old_grid, const GridView new_grid) {
  if (old_grid.rows < 3 || old_grid.cols < 3) {
    return;
  }

  // apply_stencil rejects overlapping grids, so these base pointers do not alias.
  const double* __restrict__ old_values{old_grid.values};
  double* __restrict__ new_values{new_grid.values};

  // Each row writes to a separate output range, so static partitioning is safe.
  #pragma omp parallel for schedule(static)
  for (std::size_t row = 1; row < old_grid.rows - 1; ++row) {
    const double* const above{old_values + (row - 1) * old_grid.stride};
    const double* const current{old_values + row * old_grid.stride};
    const double* const below{old_values + (row + 1) * old_grid.stride};
    double* const output{new_values + row * new_grid.stride};

    // Adjacent columns use contiguous memory and have no loop-carried writes.
    #pragma omp simd
    for (std::size_t col = 1; col < old_grid.cols - 1; ++col) {
      output[col] = 0.5 * current[col] + 0.125 * (above[col] + below[col] +
                                                  current[col - 1] + current[col + 1]);
    }
  }
}

}  // namespace uwhpc_detail

inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  if (&old_grid == &new_grid) {
    throw std::invalid_argument(
        "apply_stencil requires separate input and output grids");
  }

  if (old_grid.rows() != new_grid.rows() || old_grid.cols() != new_grid.cols()) {
    throw std::invalid_argument(
        "Input and output grids must have identical dimensions");
  }

  const ConstGridView old_view{old_grid.read_view()};
  const GridView new_view{new_grid.write_view()};

  // Complete the serial boundary phase before parallel interior writes begin.
  uwhpc_detail::copy_boundaries(old_view, new_view);
  uwhpc_detail::update_interior(old_view, new_view);
}
