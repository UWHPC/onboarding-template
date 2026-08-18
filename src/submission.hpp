#pragma once

#include <cstddef>
#include <vector>

class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<std::vector<double>> cells_;

public:
  Grid(std::size_t rows, std::size_t cols)
    : rows_{rows}
    , cols_{cols}
    , cells_(rows, std::vector<double>(cols, 0.0))
  { }

  std::size_t rows() const {
    return rows_;
  }

  std::size_t cols() const {
    return cols_;
  }

  double& operator()(std::size_t i, std::size_t j) {
    return cells_[i][j];
  }

  double  operator()(std::size_t i, std::size_t j) const {
    return cells_[i][j];
  }
};  

void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  std::size_t rows{old_grid.rows()};
  std::size_t cols{old_grid.cols()};

  for (std::size_t row{}; row < rows; ++row) {
    new_grid(row, 0) = old_grid(row, 0);
    new_grid(row, cols-1) = old_grid(row, cols-1);
  }

  for (std::size_t col{}; col < cols; ++col) {
    new_grid(0, col) = old_grid(0, col);
    new_grid(rows-1, col) = old_grid(rows-1, col);
  }

  for (std::size_t row{1}; row < rows-1; ++row) {
    for (std::size_t col{1}; col < cols-1; ++col) {
      new_grid(row, col) = (
        0.5 * old_grid(row, col) +
        0.125 * (old_grid(row-1,col) + old_grid(row+1,col) + old_grid(row,col-1) + old_grid(row,col+1))
      );
    }
  }
}
