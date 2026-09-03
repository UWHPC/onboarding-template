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
  std::vector<double> values_;

public:
  // List-initialize a vector of 0s
  Grid(std::size_t rows, std::size_t cols)
    : rows_(rows), cols_(cols), values_(rows*cols) {}

  double& operator()(std::size_t i, std::size_t j) {
      return values_[(i*cols_) + j];
  }

  double operator()(std::size_t i, std::size_t j) const {
      return values_[(i*cols_) + j];
  }

  // Accessors for grid size
  std::size_t columns() const { return cols_; }
  std::size_t rows() const { return rows_; }

  const double* rowStart(std::size_t row) const {
      return &values_[row * cols_];
  }

};

// new_arr[i][j] = 0.5   * old_arr[i][j] +
//                 0.125 * (old_arr[i-1][j] + old_arr[i+1][j] +
//                          old_arr[i][j-1] + old_arr[i][j+1]);

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {

    // Pre-fill the first and last rows with the existing old_grid values
    for (int boundary = 0; boundary < old_grid.columns(); boundary++) {
        new_grid(0, boundary) = old_grid(0, boundary);
        new_grid(old_grid.rows()-1, boundary) = old_grid(old_grid.rows()-1, boundary);
    }

    #pragma omp parallel for
    for (int i = 1; i < old_grid.rows()-1; i++) {

        // For each row, set the left and right boundaries beforehand
        new_grid(i, 0) = old_grid(i, 0);
        new_grid(i, old_grid.columns()-1) = old_grid(i, old_grid.columns()-1);

        const double* prevRow = old_grid.rowStart(i-1);
        const double* currRow = old_grid.rowStart(i);
        const double* nextRow = old_grid.rowStart(i+1);

        for (int j = 1; j < old_grid.columns()-1; j++) {
            double oldVal = currRow[j];
            double oldAbove = prevRow[j];
            double oldBelow = nextRow[j];
            double oldLeft = currRow[j-1];
            double oldRight = currRow[j+1];

            new_grid(i, j) = (0.5 * oldVal) +
                0.125 * (oldAbove  + oldBelow + oldLeft + oldRight);
        }
    }
}

// After implementing OpenMP:
// === 1 threads ===
// {"runtime_ms": 149.334, "memory_mb": 16.777, "score": 0.995}
// === 2 threads ===
// {"runtime_ms": 85.999, "memory_mb": 16.777, "score": 1.746}
// === 3 threads ===
// {"runtime_ms": 64.496, "memory_mb": 16.777, "score": 2.417}
// === 4 threads ===
// {"runtime_ms": 55.682, "memory_mb": 16.777, "score": 2.835}
// === 5 threads ===
// {"runtime_ms": 57.377, "memory_mb": 16.777, "score": 2.678}
// === 6 threads ===
// {"runtime_ms": 56.655, "memory_mb": 16.777, "score": 2.780}
// === 7 threads ===
// {"runtime_ms": 62.279, "memory_mb": 16.777, "score": 2.520}
// === 8 threads ===
// {"runtime_ms": 63.506, "memory_mb": 16.777, "score": 2.417}
