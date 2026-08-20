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
  Grid(size_t rows, size_t cols) {
      rows_ = rows;
      cols_ = cols;
      data_.resize(rows_ * cols_, 0.0);
  };

  double& operator()(size_t i, size_t j) {
      return data_[(i * cols_) + j];
  };

    std::size_t get_rows() const {
        return rows_;
    }

    std::size_t get_cols() const {
        return cols_;
    }

  double  operator()(size_t i, size_t j) const {
        return data_[(i * cols_) + j];
    };
};  

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {
    std::size_t rows = old_grid.get_rows();
    std::size_t cols = old_grid.get_cols();

    for (size_t i = 0; i < rows; i++) {
        for (size_t j = 0; j < cols; j++) {
            if ((i == 0 || i == rows - 1) || (j == 0 || j == cols - 1)) {
                new_grid(i, j) = old_grid(i, j);
            }
            else {
                new_grid(i, j) = 0.5 * old_grid(i, j) + 0.125 * (old_grid(i - 1, j) + old_grid(i + 1, j) + old_grid(i, j - 1) + old_grid(i, j + 1));
            }
        }
    }
};
