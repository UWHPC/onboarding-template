#pragma once

#include <cstddef>
#include <vector>
// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.

/*
Notes:
- Seems like we're simulating how heat spreads accross a heat conductive surface
- We represent this surface as a grid in the grid class
- It seems like the outer edges of the grid are permanently at their initial temp
- Grid class holds all temp values
- apply stencil calculates new temp
- we have 2 grids old grid and new grid
- we read each value on old grid then using the stencil function
- calculate new values and put in new grid
- i need a data structure to actually hold the grid in, probably a vector
-
*/
class Grid
{
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> cells_;

public:
  // constructor
  // What I did initially:
  // Grid(std::size_t rows, std::size_t cols)
  // {
  //   rows_ = rows;
  //   cols_ = cols;
  //   cells_ = std::vector<double>(rows * cols, 0.0);
  // }

  // using an intializer list
  // searched up and found this, it is very slightly quicker as it makes the real zero-filled
  // vector immediatley instead of making one then copying zeros everywhere
  Grid(std::size_t rows, std::size_t cols)
      : rows_{rows}, cols_{cols}, cells_(rows * cols, 0.0)
  {
  }
  // allows us to assign temperature to a square
  // returns direct access (&)to stored cell so it can be written to
  double &operator()(std::size_t i, std::size_t j)
  {
    return cells_[i * cols_ + j];
  }
  // read value at square on grid
  double operator()(std::size_t i, std::size_t j) const
  {
    return cells_[i * cols_ + j];
  }
  // will need these getters later as we cant access rows and cols as private variables
  std::size_t getRows() const
  {
    return rows_;
  }

  std::size_t getCols() const
  {
    return cols_;
  }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid &old_grid, Grid &new_grid)
{
  const std::size_t rows{old_grid.getRows()};
  const std::size_t cols{old_grid.getCols()};

  // v1: wasted time checking boundary everytime when we could just go through
  // boundary rows/cols first before calculating for all inner grids.

  /*
    for (std::size_t i = 0; i < rows; ++i)
    {
      for (std::size_t j = 0; j < cols; ++j)
      {
        const bool is_boundary{
            i == 0 || i == rows - 1 || j == 0 || j == cols - 1};

        if (is_boundary)
        {
          new_grid(i, j) = old_grid(i, j);
        }
        else
        {
          new_grid(i, j) =
              0.5 * old_grid(i, j) +
              0.125 * (old_grid(i - 1, j) +
                       old_grid(i + 1, j) +
                       old_grid(i, j - 1) +
                       old_grid(i, j + 1));
        }
      }
    }
  */

  if (rows == 0 || cols == 0)
  {
    return;
  }

  // Copy the top and bottom boundary rows.
  for (std::size_t j = 0; j < cols; ++j)
  {
    new_grid(0, j) = old_grid(0, j);
    new_grid(rows - 1, j) = old_grid(rows - 1, j);
  }

  // Copy the left and right boundary columns.
  // Start at 1 and stop before the last row, since corners are already copied.

  for (std::size_t i = 1; i < rows - 1; ++i)
  {
    new_grid(i, 0) = old_grid(i, 0);
    new_grid(i, cols - 1) = old_grid(i, cols - 1);
  }

// Calculate only non-boundary cells.
// v3 improvement: adding parallelism, disclosure; didnt know abt this before, googled
// and found this. Very neat and quite neccessary if working with large grids.
#pragma omp parallel for
  for (std::size_t i = 1; i < rows - 1; ++i)
  {
    for (std::size_t j = 1; j < cols - 1; ++j)
    {
      // formula from onboarding page
      new_grid(i, j) =
          0.5 * old_grid(i, j) +
          0.125 * (old_grid(i - 1, j) +
                   old_grid(i + 1, j) +
                   old_grid(i, j - 1) +
                   old_grid(i, j + 1));
    }
  }
}

/*
Without Parallel:
cmake --build --preset benchmark
ctest --preset benchmark --output-on-failure
./build/benchmark/uwhpc_benchmark

With Parallel:
ctest --test-dir build/benchmark-omp --output-on-failure
./build/benchmark-omp/uwhpc_benchmark
*/
