#pragma once
#include <cstddef>
#include <memory>
#include <vector>
#include <omp.h>
#include <cstring>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;

  std::unique_ptr<double[]> data_;

  // just for simplication
  // CHECK later to ensure this is row major order but I think this is right
  std::size_t index(std::size_t row, std::size_t col) const {
    return row * cols_ + col;
  }

public:
  Grid(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols), data_(std::make_unique<double []>(rows * cols)) {};

  double& operator()(std::size_t i, std::size_t j) {
    return data_[index(i, j)];
  };

  double operator()(std::size_t i, std::size_t j) const {
    return data_[index(i, j)];
  }

  double* get_data() {
    return data_.get();
  }

  const double* get_data() const {
    return data_.get();
  }

  // start with exposing some methods for getting rows/cols
  std::size_t get_row_count() const { return rows_; }
  std::size_t get_col_count() const { return cols_; }

};  

void copy_boundaries(Grid &dst, const Grid &src, std::size_t rows, std::size_t cols) {
  std::memcpy(dst.get_data(), src.get_data(), cols * sizeof(double));
  std::memcpy(dst.get_data() + (rows - 1) * cols, src.get_data() + (rows - 1) * cols, cols * sizeof(double));

  // now cols
  for (std::size_t i = 1; i < rows - 1; i++) {
    dst(i, 0) = src(i,0);
    dst(i, cols-1) = src(i, cols - 1);
  }
}

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {

  std::size_t rows = new_grid.get_row_count();
  std::size_t cols = new_grid.get_col_count();

  copy_boundaries(new_grid, old_grid, rows, cols);

  // collpase did not help
  #pragma omp parallel for schedule(static)
  for (size_t i = 1; i < rows - 1; i++) {
    #pragma omp simd

    for(size_t j = 1; j < cols - 1; j++) {
      // these are the exterior points to skip
      // actually we can just change the loop condition
      // if (i == 0 or i == rows - 1 or j == 0 or j == cols - 1) {
      //   new_grid(i,j) = old_grid(i,j);
      //   continue;
      // }
      new_grid(i, j) = 0.5 * old_grid(i,j) + 0.125 * (old_grid(i-1,j) + old_grid(i+1,j) + old_grid(i,j-1) + old_grid(i,j+1));
    }
  }
}