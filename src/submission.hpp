#pragma once

#include <cstddef>
#include <vector>
#include <cassert>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
  static const std::size_t pad_ = 8;  

  std::size_t rows_;
  std::size_t cols_;
  std::size_t stride_;
  std::vector<double> grid_; // grid compacted to 1D for fast contiguous memory lookup

public:
  Grid(std::size_t rows, std::size_t cols);

  std::size_t getRows() const;
  std::size_t getCols() const;
  std::size_t getStride() const;

  double& operator()(std::size_t i, std::size_t j);
  double  operator()(std::size_t i, std::size_t j) const;

  double* data();
  const double* data() const;
};


inline Grid::Grid(size_t rows, size_t cols) : 
  rows_(rows), 
  cols_(cols), 
  stride_(cols + pad_), // stride contains padding to avoid overlapping slots accessed in memory
  grid_(rows*stride_, 0.0) {}

inline size_t Grid::getRows() const {
  return rows_;
}

inline size_t Grid::getCols() const {
  return cols_;
}

inline size_t Grid::getStride() const {
  return stride_;
}

inline double& Grid::operator()(size_t i, size_t j) {
  return grid_[i*stride_+j];
}

inline double Grid::operator()(size_t i, size_t j) const {
  return grid_[i*stride_+j];
}

double* Grid::data() {
  return grid_.data();
}

const double* Grid::data() const {
  return grid_.data();
}


// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const size_t rows = old_grid.getRows();
  const size_t cols = old_grid.getCols();
  const size_t stride = old_grid.getStride();

  // row boundaries merged into heat diffusion block
  for (size_t j = 0; j < cols; ++j) {
    new_grid(0, j) = old_grid(0, j);
    new_grid(rows-1, j) = old_grid(rows-1, j);
  }

  // data must be distinct in old and new grids to guarantee no pointer aliasing
  assert(old_grid.data() != new_grid.data());

  // informs compiler pointers will not be aliased, optimizing SIMD
  const double* __restrict__ old = old_grid.data();
  double* __restrict__ next = new_grid.data();
  const size_t dist = stride;
  
  // divides iteration into multiple chunks spread across multiple threads
  #pragma omp parallel for schedule(static)
  for (size_t i = 1; i < rows-1; ++i) {
    const double* old_above = old + (i-1) * dist;
    const double* old_current = old + i * dist;
    const double* old_below = old + (i+1) * dist;
    double* output = next + i * dist;

    // left/right boundaries
    output[0] = old_current[0];
    output[cols-1] = old_current[cols-1];

    // allows CPU to perform same instruction to multiple data points
    #pragma omp simd
    for (size_t j = 1; j < cols-1; ++j) {
      output[j] = 0.5 * old_current[j] + 
                  0.125 * (
                    old_above[j] + old_below[j] + 
                    old_current[j-1] + old_current[j+1]
                  );
    }
  }
}