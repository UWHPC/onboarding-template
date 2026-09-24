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
  std::vector<double> data_;

public:
  Grid(std::size_t rows, std::size_t cols) 
    : rows_(rows), cols_(cols), data_(rows * cols) {}

  double& operator()(std::size_t i, std::size_t j) {
    return data_[i * cols_ + j];
  }
  double operator()(std::size_t i, std::size_t j) const{
    return data_[i * cols_ + j];
  }
  
  std::size_t rows() const { return rows_; }
  std::size_t cols() const { return cols_; }

  double* data() { return data_.data(); }
  const double* data() const { return data_.data();}

};  

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid){
  const std::size_t n = old_grid.rows();
  const std::size_t m = old_grid.cols();

  // solving pointer alliasing between old grid and new grid
  // __restrict makes it so memory access of old_grid doesn't overlap with new_grid
  const double* __restrict src = old_grid.data();
  double* __restrict dst = new_grid.data();


  // updating interior points (applying stencil)
  #pragma omp parallel for schedule(static) // parallel for loop
  for (std::size_t i = 1 ; i < n-1; i++){
    
    // computing the row for the surrounding points here instead of in every nested iteration
    const double* top = src + (i-1) * m; // get base address of row on top of i
    const double* mid = src + i * m; // get base address of i's row
    const double* bottom = src + (i+1) * m; // get base address of row beneath i
    double* out = dst + i * m; // get base address of row of for output

    #pragma GCC ivdep // allows for SIMD
    for (std::size_t j = 1; j < m-1; j++){
      // my first solution did this:
      // new_grid(i, j) = 0.5 * old_grid(i, j) + 0.125 * (old_grid(i-1, j) + old_grid(i+1, j) + old_grid(i, j-1) + old_grid(i, j+1));
      // this calculates the address 6 * m every single iteration (O(n * m) = slow)
      
      // this way we just add j to the base addresses we calculated once in iteration
      out[j] = 0.5 * mid[j] + 0.125 * (top[j] + bottom[j] + mid[j-1] + mid[j+1]);
    }
  }

  // updating boundary points (copying from old_grid)
  for (std::size_t i = 0; i < n; i++){
    new_grid(i, 0) = old_grid(i, 0); // left boundary
    new_grid(i, m-1) = old_grid(i, m-1); // right boundary
  }

  for (std::size_t j = 0; j < m; j++){
    new_grid(0, j) = old_grid(0, j); // top boundary
    new_grid(n-1, j) = old_grid(n-1, j); // bottom boundary
  }
}
