#pragma once

#include <cstddef>
#include <omp.h>
#include <vector>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> data_;

public:
  Grid(std::size_t rows, std::size_t cols)
      : rows_(rows), cols_(cols), data_(rows * cols) {};

  double &operator()(std::size_t i, std::size_t j) {
    return data_[i * cols_ + j];
  };
  double operator()(std::size_t i, std::size_t j) const {
    return data_[i * cols_ + j];
  };

  std::size_t rows() const { return rows_; }

  std::size_t cols() const { return cols_; }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid &old_grid, Grid &new_grid) {
  std::size_t rows = old_grid.rows();
  std::size_t cols = old_grid.cols();

  for (std::size_t r = 0; r < rows; r++) {
    new_grid(r, 0) = old_grid(r, 0);
    new_grid(r, cols - 1) = old_grid(r, cols - 1);
  }

  for (std::size_t c = 0; c < cols; c++) {
    new_grid(0, c) = old_grid(0, c);
    new_grid(rows - 1, c) = old_grid(rows - 1, c);
  }

#pragma omp parallel for
  for (std::size_t r = 1; r < rows - 1; r++) {
    for (std::size_t c = 1; c < cols - 1; c++) {
      new_grid(r, c) = 0.5 * old_grid(r, c) +
                       0.125 * (old_grid(r - 1, c) + old_grid(r + 1, c) +
                                old_grid(r, c - 1) + old_grid(r, c + 1));
    }
  }
};
