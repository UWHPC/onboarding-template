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
  std::vector<double> grid_;
public:
  // set rows and cols
  Grid(std::size_t rows, std::size_t cols) {
    rows_ = rows;
    cols_ = cols;
    // set all elements in grid to 0.0
    grid_ = std::vector<double>(rows * cols, 0.0);
  }
  // row major order
  // read/write access
  double& operator()(std::size_t i, std::size_t j) {
    return grid_[i * cols_ + j];
  }
  // read-only access
  double  operator()(std::size_t i, std::size_t j) const {
    return grid_[i * cols_ + j];
  }
  // getter for rows read-only
  std::size_t get_rows() const {
    return rows_;
  }
  // getter for cols read-only
  std::size_t get_cols() const {
    return cols_;
  }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  for (std::size_t i = 1; i < old_grid.get_rows() - 1; i++) {
    for (std::size_t j = 1; j < old_grid.get_cols() - 1; j++) {
        // update all points and reset boundary points after
        new_grid(i,j) = 0.5 * old_grid(i,j) + 0.125 * (old_grid(i-1,j) + old_grid(i+1,j) + old_grid(i,j-1) + old_grid(i,j+1));
    }
  }
  // copying boundary points
  for (std::size_t i = 0; i < old_grid.get_cols(); i++) {
    new_grid(0,i) = old_grid(0,i); // top row
    new_grid(old_grid.get_rows()-1,i) = old_grid(old_grid.get_rows()-1,i); // bottom row
  }
  for (std::size_t j = 0; j < old_grid.get_rows(); j++) {
    new_grid(j,0) = old_grid(j,0); // left column
    new_grid(j,old_grid.get_cols()-1) = old_grid(j,old_grid.get_cols()-1); // right column
  }
}
