#pragma once

#include <cstddef>
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
  Grid(std::size_t rows, std::size_t cols);

  double& operator()(std::size_t i, std::size_t j);
  double  operator()(std::size_t i, std::size_t j) const;

  std::size_t get_rows() const { return rows_; }
  std::size_t get_cols() const { return cols_; }
  double* data() { return data_.data(); }
  const double* data() const { return data_.data(); }
};  

Grid::Grid(std::size_t rows, std::size_t cols) : 
rows_(rows), cols_(cols), data_(rows * cols, 0.0) {}

double& Grid::operator()(std::size_t i, std::size_t j) {
  return data_[i * cols_ + j];
}

double Grid::operator()(std::size_t i, std::size_t j) const {
  return data_[i * cols_ + j];
}



// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid);

void apply_stencil(const Grid& old_grid, Grid& new_grid) {
    std::size_t rows = old_grid.get_rows();
    std::size_t cols = old_grid.get_cols();
    std::size_t stride = cols;

    for (std::size_t i = 0; i < cols; ++i) {
      new_grid(0, i) = old_grid(0, i);
      new_grid(rows - 1, i) = old_grid(rows - 1, i);
    }
    if (rows < 3 || cols < 3) return;

    const double* __restrict old_grid_data = old_grid.data();
    double* __restrict new_grid_data = new_grid.data();

    #pragma omp parallel for schedule(static)
    for (std::size_t i = 1; i < rows - 1; ++i) {
      std::size_t row = i * stride;
      std::size_t next_row = row + stride;
      std::size_t last_row = row - stride;

      new_grid_data[row] = old_grid_data[row];
      new_grid_data[row + cols - 1] = old_grid_data[row + cols - 1];
      
    #ifdef _OPENMP
      #pragma omp simd
    #endif

      for (std::size_t j = 1; j < cols - 1; ++j) {
        new_grid_data[row + j] = 0.5  * old_grid_data[row + j] +
        0.125 * (old_grid_data[last_row + j] + old_grid_data[next_row + j] +
                  old_grid_data[row + j - 1] + old_grid_data[row + j + 1]);

      }
    }
}