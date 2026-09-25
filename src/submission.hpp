#pragma once

#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cmath>
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
    std::size_t stride_;
    // additions from here

  public:
    std::vector<double> data;
    Grid(std::size_t rows, std::size_t cols)
      : rows_{rows}
    , cols_{cols}
    , stride_{(cols_ + 7) & (~7ULL)}
    {
      // check if stride is power of two to remove cache aliasing
      if ((stride_ & (stride_ - 1)) == 0) {
        stride_ += 8;
      } 

      data.resize(rows * stride_);
    };


    double& operator()(std::size_t i, std::size_t j) {return data[i * stride_ + j];};
    double  operator()(std::size_t i, std::size_t j) const {return data[i * stride_ + j];};
    std::size_t rows() const{return rows_;};
    std::size_t cols() const{return cols_;};
    std::size_t stride() const{return stride_;};
};  

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const std::size_t rows = old_grid.rows();
  const std::size_t cols = old_grid.cols();
  const std::size_t stride = old_grid.stride();

// Copy first and last row
  std::copy(
      old_grid.data.begin(),
      old_grid.data.begin() + cols,
      new_grid.data.begin());
  
  size_t last_row = stride * (rows - 1);
  std::copy(
    old_grid.data.begin() + last_row,
    old_grid.data.begin() + last_row + cols,
    new_grid.data.begin() + last_row);

// Copy first and last column
  for(std::size_t i = 1; i < rows - 1; ++i) {
    new_grid(i, 0)        = old_grid(i, 0);
    new_grid(i, cols - 1) = old_grid(i, cols-1);
  }
// Prevent accessing outside memory
  if (rows < 3 || cols < 3) {
    return;
  }

#pragma omp parallel for schedule(static)
  for(std::size_t i = 1; i < rows - 1; ++i) {
// compute pointers to crawl along with the index
    const double* __restrict__ prev = old_grid.data.data()  + (i-1) * stride; // North
    const double* __restrict__ cur = old_grid.data.data()   + i * stride;     // Center
    const double* __restrict__ next = old_grid.data.data()  + (i+1) * stride; // South
    double* __restrict__ to = new_grid.data.data()          + i * stride;     // Destination

// The compiler automatically converts the below to SIMD instructions.
#pragma omp simd
    for(std::size_t j = 1; j < cols - 1 ; ++j) {
      // Perform the stencil operation: (north + west + south + east) * 0.125 + center * 0.5
      to[j] = std::fma((prev[j] + next[j] + cur[j-1]+ cur[j+1]), 0.125, cur[j] * 0.5);
    }
  }
};


