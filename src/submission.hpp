#pragma once

#include <cstddef>
#include <vector>

class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> data_;

public:
  Grid(std::size_t rows, std::size_t cols)
    : rows_(rows), cols_(cols), data_(rows * cols, 0.0) {}

  // 2D access
  double& operator()(std::size_t i, std::size_t j) {
    return data_[i * cols_ + j];
  }

  // Read only access
  double operator()(std::size_t i, std::size_t j) const {
    return data_[i * cols_ + j];
  }

  std::size_t rows() const {
    return rows_;
  }

  std::size_t cols() const {
    return cols_;
  }

  // Pointer to row i
  double* row(std::size_t i) {
    return data_.data() + i * cols_;
  }

  // Read only pointer
  const double* row(std::size_t i) const {
    return data_.data() + i * cols_;
  }
};


inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {

  std::size_t rows = old_grid.rows();
  std::size_t cols = old_grid.cols();

  // Top boundary
  for (std::size_t j = 0; j < cols; ++j) {
    new_grid(0, j) = old_grid(0, j);
  }

  // Bottom boundary
  for (std::size_t j = 0; j < cols; ++j) {
    new_grid(rows - 1, j) = old_grid(rows - 1, j);
  }

  // Left boundary
  for (std::size_t i = 0; i < rows; ++i) {
    new_grid(i, 0) = old_grid(i, 0);
  }

  // Right boundary
  for (std::size_t i = 0; i < rows; ++i) {
    new_grid(i, cols - 1) = old_grid(i, cols - 1);
  }

  #pragma omp parallel for
  for (std::size_t i = 1; i < rows - 1; ++i) {

    const double* up = old_grid.row(i - 1);
    const double* mid = old_grid.row(i);
    const double* down = old_grid.row(i + 1);

    double* out = new_grid.row(i);

    for (std::size_t j = 1; j < cols - 1; ++j) {
      out[j] = 0.5 * mid[j] + 0.125 * (up[j] + down[j] + mid[j - 1] + mid[j + 1]);
    }
  }
}