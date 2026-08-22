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
  const std::size_t rows_;
  const std::size_t cols_;
// additions from here

public:
  std::vector<double> data;
  Grid(std::size_t rows, std::size_t cols)
    : rows_{rows}
    , cols_{cols}
    , data{std::vector(rows * cols, 0.0)}
  {};

  double& operator()(std::size_t i, std::size_t j) {return data[i * cols_ + j];};
  double  operator()(std::size_t i, std::size_t j) const {return data[i * cols_ + j];};
  std::size_t rows() const{return rows_;};
  std::size_t cols() const{return cols_;};
};  

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const std::size_t rows = old_grid.rows();
  const std::size_t cols = old_grid.cols();
  
  std::memcpy(&new_grid.data[0], &old_grid.data[0], cols * sizeof(double));
  std::memcpy(
      &new_grid.data[cols * (rows-1)],
      &old_grid.data[cols * (rows-1)],
      cols * sizeof(double)
      );

  for (std::size_t i = 1; i < rows - 1; ++i) {
    // avoid function overhead
    new_grid.data[i * cols] = old_grid.data[i * cols];
    new_grid.data[(i+1) * cols - 1] = old_grid.data[(i+1) * cols - 1] ;
  }

  #pragma omp parallel for
  for(std::size_t i = 1; i < rows - 1; ++i) {
    const double* prev = old_grid.data.data() + (i-1) * cols;
    const double* cur = old_grid.data.data() + i * cols;
    const double* next = old_grid.data.data() + (i+1) * cols;
    double* to = new_grid.data.data() + i * cols;

  #pragma omp simd
    for(std::size_t j = 1; j < cols - 1; ++j) {
      to[j] = (prev[j] + cur[j-1] + cur[j] * 4.0 + cur[j+1] + next[j]) * 0.125;
    }
  }
};

