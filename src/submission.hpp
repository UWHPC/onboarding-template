#pragma once

#include <cstddef>
#include <cstring>
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
  std::vector<double> cells_;

public:
  Grid(std::size_t rows, std::size_t cols);

  std::size_t rows() const { return rows_; }
  std::size_t cols() const { return cols_; }

  double* data() { return cells_.data(); }
  const double* data() const { return cells_.data(); }

  double& operator()(std::size_t i, std::size_t j);
  double  operator()(std::size_t i, std::size_t j) const;
};  

Grid::Grid(std::size_t rows, std::size_t cols)
  : rows_{rows}
  , cols_{cols}
  , cells_(rows * cols, 0.0)
{}

double& Grid::operator()(std::size_t i, std::size_t j) {
  return cells_[i*cols_ + j];
}

double Grid::operator()(std::size_t i, std::size_t j) const {
  return cells_[i*cols_ + j];
}

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const double* __restrict old_data = old_grid.data();
  double* __restrict new_data = new_grid.data();

  const int rows_i = static_cast<int>(old_grid.rows());
  const int cols_i = static_cast<int>(old_grid.cols());

  std::memcpy(new_data, old_data, cols_i * sizeof(double));
  std::memcpy(new_data + (rows_i - 1) * cols_i, old_data + (rows_i - 1) * cols_i, cols_i * sizeof(double));

  #pragma omp parallel for
  for (int i = 1; i < rows_i - 1; ++i) {
    const int row_offset = i * cols_i;

    new_data[row_offset] = old_data[row_offset];
    new_data[row_offset + cols_i - 1] = old_data[row_offset + cols_i - 1];

    #pragma omp simd
    for (int j = 1; j < cols_i - 1; ++j) {
      const int index = row_offset + j;
      new_data[index] = 0.5 * old_data[index] + 0.125 * (old_data[index - cols_i] + old_data[index + cols_i] + old_data[index - 1] + old_data[index + 1]);
    }
  }
}