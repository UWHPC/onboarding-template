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
  std::vector<double> grid;

public:
  Grid(std::size_t rows, std::size_t cols): rows_{rows}, cols_{cols}, grid(rows * cols) {}

  std::pair<std::size_t, std::size_t> getDimensions() const {
    return {rows_, cols_};
  }

  double& operator()(std::size_t i, std::size_t j) {
    return grid[i*cols_ + j];
  }
  double  operator()(std::size_t i, std::size_t j) const {
    return grid[i*cols_ + j];
  };

  double* data() { return grid.data(); }
  const double* data() const { return grid.data(); }
};  

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid)  {
  const auto [old_rows, old_cols] = old_grid.getDimensions();
  if (old_rows == 0 || old_cols == 0) {
    return;
  }

  const double* old_data{old_grid.data()};
  double* new_data{new_grid.data()};

  for (std::size_t i = 0; i < old_rows; ++i) {
    new_data[i*old_cols] = old_data[i*old_cols];
    new_data[i*old_cols + old_cols - 1] = old_data[i*old_cols + old_cols - 1];
  }

  for (std::size_t j = 0; j < old_cols; ++j) {
    new_data[j] = old_data[j];
    new_data[(old_rows-1)*old_cols + j] = old_data[(old_rows-1)*old_cols + j];
  }

  #pragma omp parallel for
  for (std::size_t i = 1; i < old_rows-1; ++i) {
    const double* old_row{old_data + i*old_cols};
    double* new_row{new_data + i*old_cols};

    #pragma omp simd
    for (std::size_t j = 1; j < old_cols-1; ++j) {
      new_row[j] = 0.5 * old_row[j] + 0.125 * (old_row[j-old_cols] + old_row[j+old_cols] + old_row[j-1] + old_row[j+1]);
    }
  }
}
