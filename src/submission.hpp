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
  std::vector<double> arr_;

public:
  Grid(std::size_t rows, std::size_t cols)
  {
    rows_ = rows;
    cols_ = cols;
    arr_.assign(rows * cols, 0.0);
  }
  double operator()(std::size_t i, std::size_t j) const
  {
    return arr_[i * cols_ + j];
  }
  double &operator()(std::size_t i, std::size_t j)
  {
    return arr_[i * cols_ + j];
  }

  std::size_t rows() const
  {
    return rows_;
  }

  std::size_t cols() const
  {
    return cols_;
  }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid &old_grid, Grid &new_grid);

void apply_stencil(const Grid &old_grid, Grid &new_grid)
{
  // store rows and cols from old grid to local vars
  size_t rows = old_grid.rows();
  size_t cols = old_grid.cols();

  // interior points
  for (size_t i = 1; i < rows - 1; ++i)
  {
    for (size_t j = 1; j < cols - 1; ++j)
    {
      new_grid(i, j) = 0.5  * old_grid(i, j) +
              0.125 * (old_grid(i-1, j) + old_grid(i+1, j) +
                      old_grid(i, j-1) + old_grid(i, j+1));
    }
  }
  
  // boundaries
  // top row where i=0 
  for (size_t j = 0; j < cols; ++j)
  {
    new_grid(0, j) = old_grid(0, j);
  }

  // bottom row where i=rows-1 (last row index)
  for (size_t j = 0; j < cols; ++j)
  {
    new_grid(rows - 1, j) = old_grid(rows - 1, j);
  }

  // left column where j = 0
  for (size_t i = 1; i < rows - 1; ++i)
  {
    new_grid(i, 0) = old_grid(i, 0);
  }

  // right column where j = cols-1 (last column index)
  for (size_t i = 1; i < rows - 1; ++i)
  {
    new_grid(i, cols - 1) = old_grid(i, cols - 1);
  }
}
