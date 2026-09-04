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
  // Initialize the grid to a vector of 0s
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

  // Return the first value in contiguous memory
  const double* data() const {
      return values_.data();
  }

   double* data() {
      return values_.data();
  }

};

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
    for (std::size_t i = 1; i < rows-1; i++) {
        // Set pointers to the start of all 3 required rows so that we don't have to keep recalculating the position of a cell using operator()
        const double* prevRow = &input[(i-1)*cols];
        const double* currRow = &input[(i)*cols];
        const double* nextRow = &input[(i+1)*cols];

        double* outputRow = &output[i*cols];

        // For each row, set the left and right boundaries beforehand
        outputRow[0] = currRow[0];
        outputRow[cols - 1] = currRow[cols - 1];

        for (std::size_t j = 1; j < cols-1; j++) {
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
