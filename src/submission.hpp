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

  const double* rowStart(int row) const {
      return &values_[row * cols_];
  }

  double* rowStart(int row) {
      return &values_[row * cols_];
  }

  // Return the first value in contiguous memory
  const double* data() const {
      return values_.data();
  }

   double* data() {
      return values_.data();
  }

};

// new_arr[i][j] = 0.5   * old_arr[i][j] +
//                 0.125 * (old_arr[i-1][j] + old_arr[i+1][j] +
//                          old_arr[i][j-1] + old_arr[i][j+1]);

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {

    std::size_t rows = old_grid.rows();
    std::size_t cols = old_grid.columns();

    // Promise to the compiler that the input and output do not overlap
    const double* __restrict__ input = old_grid.data();
    double*  __restrict__ output = new_grid.data();

    // Pre-fill the first and last rows with the existing old_grid values
    std::memcpy(output, input, cols * sizeof(double));
    std::memcpy(&output[(rows-1)*cols], &input[(rows-1)*cols], cols*sizeof(double));

    // OpenMP compiler directive will split the calculations for all rows between multiple threads so that they can happen simultaneously
    // The task is thread-safe because old_grid is not being modified, and the new_grid values that are being modified are separate from each other
    #pragma omp parallel for
    for (int i = 1; i < rows-1; i++) {

        // For each row, set the left and right boundaries beforehand
        new_grid(i, 0) = old_grid(i, 0);
        new_grid(i, cols-1) = old_grid(i, cols-1);

        // Set pointers to the start of all 3 required rows so that we don't have to keep recalculating the position of a cell using operator()
        const double* prevRow = &input[(i-1)*cols];
        const double* currRow = &input[(i)*cols];
        const double* nextRow = &input[(i+1)*cols];

        double* outputRow = &output[i*cols];

        for (int j = 1; j < cols-1; j++) {
            double oldVal = currRow[j];
            double oldAbove = prevRow[j];
            double oldBelow = nextRow[j];
            double oldLeft = currRow[j-1];
            double oldRight = currRow[j+1];

            outputRow[j] = (0.5 * oldVal) +
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
