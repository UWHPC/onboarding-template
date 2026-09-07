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
  Grid(std::size_t rows, std::size_t cols) : rows_ {rows}, cols_ {cols}, grid(rows*cols, 0) {}

  double& operator()(std::size_t i, std::size_t j) {
    return grid[i * cols_ + j];
  }

  double operator()(std::size_t i, std::size_t j) const {
    return grid[i * cols_ + j];
  }

  std::size_t getRows() const {
    return rows_;
  }

  std::size_t getCols() const {
    return cols_;
  }
};  

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {

  // DEFINING THE CONSTANTS
  const std::size_t ROWS = old_grid.getRows();
  const std::size_t COLS = old_grid.getCols();

  // SETTING THE EXTERIOR POINTS 
  for (std::size_t r = 0; r < ROWS; ++r) {
    new_grid(r, 0) = old_grid(r, 0);
    new_grid(r, COLS - 1) = old_grid(r, COLS - 1);
  }

  for (std::size_t c = 0; c < COLS; ++c) {
    new_grid(0, c) = old_grid(0,c);
    new_grid(ROWS - 1, c) = old_grid(ROWS - 1, c);
  }

  // SETTING THE INTERIOR POINTS
  for (std::size_t r = 1; r < ROWS - 1; ++r) {
    for (std::size_t c = 1; c < COLS - 1; ++c) {
      new_grid(r, c) = 0.5 * old_grid(r, c) +
                       0.125 * (old_grid(r - 1, c) + old_grid(r + 1, c) + 
                                old_grid(r, c - 1) + old_grid(r, c + 1)); 
    }
  }
}
