#pragma once

#include <cstddef>
#include <vector>
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

  std::vector<double> grid_;

public:
  Grid(std::size_t rows, std::size_t cols) : rows_ {rows}, cols_ {cols}, grid_(rows*cols) {}

  double& operator()(std::size_t i, std::size_t j) {
    return grid_[i * cols_ + j];
  }

  double operator()(std::size_t i, std::size_t j) const {
    return grid_[i * cols_ + j];
  }

  std::size_t getRows() const {
    return rows_;
  }

  std::size_t getCols() const {
    return cols_;
  }

  double* data() {
    return grid_.data();
  }

  const double* data() const {
    return grid_.data();
  }

};  

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {

  // Defining the constants
  const std::ptrdiff_t ROWS = static_cast<std::ptrdiff_t>(old_grid.getRows());
  const std::ptrdiff_t COLS = static_cast<std::ptrdiff_t>(old_grid.getCols());

  // Getting pointers to the data with restrict to allow vectorization
  const double* __restrict old_p = old_grid.data();
  double* __restrict new_p = new_grid.data();

  // Copying the first and last columns
  const std::ptrdiff_t LAST_COL = COLS - 1; 

  for (std::ptrdiff_t r = 0; r < ROWS; ++r) {
    new_p[r * COLS] = old_p[r * COLS];
    new_p[r * COLS + LAST_COL] = old_p[r * COLS + LAST_COL];
  }

  // Copying the first and last rows with memcpy
  const std::ptrdiff_t LAST_ROW_POS = (ROWS - 1) * (COLS);

  std::memcpy(new_p, old_p, COLS * sizeof(double));
  std::memcpy(new_p + LAST_ROW_POS, old_p + LAST_ROW_POS, COLS * sizeof(double));

  // Splitting interior rows across threads
  #pragma omp parallel for schedule(static)
  for (std::ptrdiff_t r = 1; r < ROWS - 1; ++r) {
    
    // Precalculating variables based on 'r'
    const double* up = old_p + (r - 1)*COLS;
    const double* mid = old_p + r * COLS;
    const double* down = old_p + (r + 1)*COLS;
    double* out = new_p + r * COLS;

    for (std::ptrdiff_t c = 1; c < COLS - 1; ++c) {
      out[c] = 0.5*mid[c] + 0.125*(up[c] + down[c] + mid[c - 1] + mid[c + 1]);
    }
  }
}
