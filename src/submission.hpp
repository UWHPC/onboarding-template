#pragma once

#include <cstddef>
#include <vector>

// Stage 1: correct serial baseline.
// Storage is one flat, row-major std::vector: cell (i, j) lives at i * cols_ + j.
// One allocation keeps each row contiguous, so the inner column loop walks
// memory in order, and std::vector gives zero-initialization and safe
// copy/move/cleanup for free.
class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> data_;

public:
  Grid(std::size_t rows, std::size_t cols)
    : rows_{rows}, cols_{cols}, data_(rows * cols, 0.0) {}

  double& operator()(std::size_t i, std::size_t j) { return data_[i * cols_ + j]; }
  double  operator()(std::size_t i, std::size_t j) const { return data_[i * cols_ + j]; }

  // The harness never asks for dimensions, but the stencil needs them.
  std::size_t rows() const { return rows_; }
  std::size_t cols() const { return cols_; }
};

// Marked inline because this is a header: without it, two .cpp files that
// include it would each define apply_stencil and the linker would reject it.
inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const std::size_t rows = old_grid.rows();
  const std::size_t cols = old_grid.cols();

  if (rows == 0 || cols == 0) {
    return;  // nothing to copy; also avoids rows - 1 wrapping around to a huge number
  }

  // Boundary: top and bottom rows are copied unchanged.
  for (std::size_t j = 0; j < cols; ++j) {
    new_grid(0, j) = old_grid(0, j);
    new_grid(rows - 1, j) = old_grid(rows - 1, j);
  }

  // Boundary: left and right columns (corners were already done above).
  for (std::size_t i = 1; i + 1 < rows; ++i) {
    new_grid(i, 0) = old_grid(i, 0);
    new_grid(i, cols - 1) = old_grid(i, cols - 1);
  }

  // Interior: the five-point weighted average.
  for (std::size_t i = 1; i + 1 < rows; ++i) {
    for (std::size_t j = 1; j + 1 < cols; ++j) {
      new_grid(i, j) = 0.5 * old_grid(i, j) +
                       0.125 * (old_grid(i - 1, j) + old_grid(i + 1, j) +
                                old_grid(i, j - 1) + old_grid(i, j + 1));
    }
  }
}
