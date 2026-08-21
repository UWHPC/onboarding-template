#pragma once

#include <stdexcept>
#include <vector>


class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> data_;

public:
  Grid(std::size_t rows, std::size_t cols);

  double& operator()(std::size_t i, std::size_t j);
  double operator()(std::size_t i, std::size_t j) const;

  std::size_t rows() const;
  std::size_t cols() const;
};  

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid);


inline Grid::Grid(std::size_t rows, std::size_t cols) 
  : rows_(rows),
    cols_(cols),
    data_(rows * cols, 0.0) {}


inline double& Grid::operator()(std::size_t i, std::size_t j) {
  return data_[i * cols_ + j];
}


inline double Grid::operator()(std::size_t i, std::size_t j) const {
  return data_[i * cols_ + j];
}


inline std::size_t Grid::rows() const {
  return rows_;
}


inline std::size_t Grid::cols() const {
  return cols_;
}


inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const std::size_t rows = new_grid.rows();
  const std::size_t cols = new_grid.cols();

  // First and last row copy
  for (std::size_t j = 0; j < cols; j++) {
    new_grid(0, j) = old_grid(0, j);
    new_grid(rows - 1, j) = old_grid(rows - 1, j);
  }

  // Sides copy
  for (std::size_t i = 1; i < rows - 1; i++) {
    new_grid(i, 0) = old_grid(i, 0);
    new_grid(i, cols - 1) = old_grid(i, cols - 1);
  }

  // Inner grid
  for (std::size_t i = 1; i < rows - 1; i++) {
    for (std::size_t j = 1; j < cols - 1; j++) {
      const double center = old_grid(i, j);
      const double north = old_grid(i - 1, j);
      const double south = old_grid(i + 1, j);
      const double east = old_grid(i, j + 1);
      const double west = old_grid(i, j - 1);

      new_grid(i, j) = 0.5 * center + 
                          0.125 * (north + south + 
                                    east + west);
    }
  }
}
