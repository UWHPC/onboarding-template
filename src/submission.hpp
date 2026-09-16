#pragma once

#include <cstddef>
#include <vector>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid
{
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> grid_;

public:
  Grid(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols), grid_(std::vector<double>(rows * cols)) {}

  double &operator()(std::size_t i, std::size_t j)
  {
    return this->grid_[i * this->cols_ + j];
  }
  double operator()(std::size_t i, std::size_t j) const
  {
    return this->grid_[i * this->cols_ + j];
  }

  size_t rows() const { return this->rows_; }
  size_t cols() const { return this->cols_; }

  const double *data() const { return this->grid_.data(); }
  double *data() { return this->grid_.data(); }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid &old_grid, Grid &new_grid)
{
  size_t rows = old_grid.rows();
  size_t cols = old_grid.cols();

  const double *__restrict__ old_grid_data = old_grid.data();
  double *__restrict__ new_grid_data = new_grid.data();

#pragma omp parallel
  {
#pragma omp for
    for (std::size_t i = 0; i < rows; ++i)
    {
      std::size_t row = i * cols;
      new_grid_data[row] = old_grid_data[row];
      new_grid_data[row + cols - 1] = old_grid_data[row + cols - 1];
    }

#pragma omp for
    for (std::size_t j = 0; j < cols; ++j)
    {
      new_grid_data[j] = old_grid_data[j];
      new_grid_data[(rows - 1) * cols + j] = old_grid_data[(rows - 1) * cols + j];
    }

#pragma omp for
    for (std::size_t i = 1; i < rows - 1; ++i)
    {
      std::size_t row = i * cols;
#pragma omp simd
      for (std::size_t j = 1; j < cols - 1; ++j)
      {
        new_grid_data[row + j] = 0.5 * old_grid_data[row + j] +
                                 0.125 * (old_grid_data[row - cols + j] + old_grid_data[row + cols + j] +
                                          old_grid_data[row + j - 1] + old_grid_data[row + j + 1]);
      }
    }
  }
}
