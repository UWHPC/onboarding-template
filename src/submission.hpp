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

  double* data() {
    return grid.data();
  }

  const double* data() const {
    return grid.data();
  }

};  

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {

  // Defining the constants
  const std::size_t ROWS = old_grid.getRows();
  const std::size_t COLS = old_grid.getCols();

  // Getting pointers to the data with restrict to allow vectorization
  const double* __restrict old_p = old_grid.data();
  double* __restrict new_p = new_grid.data();

  // Setting the exterior points
  // First and last columns
  const std::size_t LAST_COL = COLS - 1; 

  for (std::size_t r = 0; r < ROWS; ++r) {
    new_p[r * COLS] = old_p[r * COLS];
    new_p[r * COLS + LAST_COL] = old_p[r * COLS + LAST_COL];
  }

  // First and last rows
  const std::size_t LAST_ROW_POS = (ROWS - 1) * (COLS);

  for (std::size_t c = 0; c < COLS; ++c) {
    new_p[c] = old_p[c];
    new_p[LAST_ROW_POS + c] = old_p[LAST_ROW_POS + c];
  }

  // Setting the interior points
  for (std::size_t r = 1; r < ROWS - 1; ++r) {
    
    // Precalculating variables based on 'r'
    const double* up = old_p + (r - 1)*COLS;
    const double* mid = old_p + r * COLS;
    const double* down = old_p + (r + 1)*COLS;
    double* out = new_p + r * COLS;

    for (std::size_t c = 1; c < COLS - 1; ++c) {
      out[c] = 0.5*mid[c] + 0.125*(up[c] + down[c] + mid[c - 1] + mid[c + 1]);
    }
  }
}
