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
  //The temperature grid is stored as one flat row-major vector. A single allocation with
  //rows back-to-back makes it so that the stencil's inner loop walks contiguous memory
  //std::vector handles zero-init and cleanup
  std::vector<double> grid_;

public:
  Grid(std::size_t rows, std::size_t cols);

  double& operator()(std::size_t i, std::size_t j);
  double  operator()(std::size_t i, std::size_t j) const;

  //Pointer to first element of a row, so kernel works out the row offset once per row 
  //instead of once per cell. Doesn't own anything. Only valid while the Grid is alive.
  double* get_row_start(const std::size_t row);
  const double* get_row_start(const std::size_t row) const;

  std::size_t rows() const;
  std::size_t cols() const;
};  

// Everything is defined in the header, so all functions are marked inline to avoid
//duplicate-definition link errors if more than one .cpp includes this file

 inline double* Grid::get_row_start(const std::size_t row){
  return grid_.data() + row * cols_;
}

 inline const double* Grid::get_row_start(const std::size_t row) const{
  return grid_.data() + row * cols_;
} 

inline Grid::Grid(std::size_t rows, std::size_t cols)
: rows_(rows), cols_(cols), grid_(rows * cols) {
} 

inline std::size_t Grid::rows() const {
  return rows_;
}

inline std::size_t Grid::cols() const {
  return cols_;
}

inline double& Grid::operator()(std::size_t i, std::size_t j) {
  return grid_[(i * cols_) + j];
}

inline double Grid::operator()(std::size_t i, std::size_t j) const {
  return grid_[(i * cols_) + j];
}


// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid);

inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  //Boundary is copied first (serially), then the interior is split across threads.
  //The two phases write different cells, so no cell is written by more than one thread

  //The boundary is ~0.4% of the cells on the benchmark grid, so these loops stay simple
  // and just use operator()
  for (std::size_t col = 0; col < old_grid.cols(); col++)
  {
    new_grid(0, col) = old_grid(0, col);
    new_grid(old_grid.rows() - 1, col) = old_grid(old_grid.rows() - 1, col);
  }

  for (std::size_t row = 0; row < old_grid.rows(); row++)
  {
    new_grid(row, 0) = old_grid(row, 0);
    new_grid(row, old_grid.cols() - 1) = old_grid(row, old_grid.cols() - 1);
  }
  
  //Each output row only reads old_grid and costs about the same, so a plain
  //static split across threads works here.
  //No hardcoded thread count - scaling falls off past physical core count,
  //but that's specific to whatever hardware runs this, so it's left to OpenMP.

  #pragma omp parallel for
  for (std::size_t row = 1; row < old_grid.rows() - 1; row++)
  {
    //old_grid and new_grid are never aliased in this harness, so restrict here is safe
    const double* __restrict old_grid_row_above = old_grid.get_row_start(row - 1);
    const double* __restrict old_grid_row_below = old_grid.get_row_start(row + 1);
    const double* __restrict old_grid_current_row = old_grid.get_row_start(row);
    double* __restrict new_grid_current_row = new_grid.get_row_start(row); 

    //no #pragma omp simd here - this loop already auto-vectorizes under -O3 (checked
    //the vectorization report), and the pragma consistently made it slower when benchmarked

    for (std::size_t col = 1; col < old_grid.cols() - 1; col++)
    {
      new_grid_current_row[col] = 0.5 * old_grid_current_row[col] + 0.125 * (old_grid_row_below[col] + 
      old_grid_row_above[col] + old_grid_current_row[col - 1] + old_grid_current_row[col + 1]);
    }
  }
}