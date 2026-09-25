#pragma once
#include <vector>
#include <cstddef>

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
  std::vector<double> data_;

public:
  Grid(std::size_t rows, std::size_t cols) : rows_{rows}, cols_{cols}, data_(rows * cols) {};

  double &operator()(std::size_t i, std::size_t j)
  {
    return data_[i * cols_ + j];
  };

  double operator()(std::size_t i, std::size_t j) const
  {
    return data_[i * cols_ + j];
  };
  double &index(std::size_t x)
  {
    return data_[x];
  }

  double index(std::size_t x) const
  {
    return data_[x];
  }

  std::size_t Rows() const { return rows_; }
  std::size_t Cols() const { return cols_; }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.

double stencil(double point, double left, double right, double up, double down)
{
  return 0.5 * point +
         0.125 * (left + right +
                  down + up);
}

void apply_stencil(const Grid &old_grid, Grid &new_grid)
{
  const std::size_t rows = old_grid.Rows();
  const std::size_t cols = old_grid.Cols();

// if (rows <= 1 || cols <= 1)
// {
//   return;
// }
#pragma omp parallel for
  for (std::size_t i = cols; i < (rows) * (cols)-cols; ++i)
  {
    // std::size_t row{i / (cols - 2) + 1};
    // std::size_t col{i % (cols - 2) + 1};
    // if (i >= cols && i <= (rows - 1) * cols)
    new_grid.index(i) = stencil(old_grid.index(i), old_grid.index(i - cols), old_grid.index(i + cols), old_grid.index(i + 1), old_grid.index(i - 1));
  }

  for (std::size_t i{0}; i < rows; ++i)
  {
    new_grid(i, 0) = old_grid(i, 0);
    new_grid(i, cols - 1) = old_grid(i, cols - 1);
    // new_grid(i, 0) = 0;
    // new_grid(i, cols - 1) = 0;
  }

  for (std::size_t j{0}; j < cols; ++j)
  {
    new_grid(0, j) = old_grid(0, j);
    new_grid(rows - 1, j) = old_grid(rows - 1, j);
    // new_grid(0, j) = 0;
    // new_grid(rows - 1, j) = 0;
  }
}
