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
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {

}
