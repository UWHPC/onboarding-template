#pragma once
#include <cstddef>
#include <cstring>
#include <vector>

class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> data_;

public:
  Grid(std::size_t rows, std::size_t cols)
      : rows_{rows}, cols_{cols}, data_(rows * cols, 0.0) {}

  double &operator()(std::size_t i, std::size_t j) {
    return data_[i * cols_ + j];
  }

  double operator()(std::size_t i, std::size_t j) const {
    return data_[i * cols_ + j];
  }

  std::size_t rows() const { return rows_; }

  std::size_t cols() const { return cols_; }

  double *data() { return data_.data(); }

  const double *data() const { return data_.data(); }
};

inline void apply_stencil(const Grid &old_grid, Grid &new_grid) {
  std::size_t rows = old_grid.rows();
  std::size_t cols = old_grid.cols();
  // source starting point
  const double *src_add = old_grid.data();
  double *dst_add = new_grid.data();

  std::memcpy(dst_add, src_add, cols * sizeof(double));
  std::memcpy((dst_add + (cols * (rows - 1))), (src_add + (cols * (rows - 1))),
              cols * sizeof(double));

  // left column
  for (std::size_t i = 0; i < rows; ++i) {
    new_grid(i, 0) = old_grid(i, 0);
  }

  // right column
  for (std::size_t i = 0; i < rows; ++i) {
    new_grid(i, cols - 1) = old_grid(i, cols - 1);
  }

#pragma omp parallel for
  for (std::size_t i = 1; i < rows - 1; ++i) {
    for (std::size_t j = 1; j < cols - 1; ++j) {
      new_grid(i, j) = 0.5 * old_grid(i, j) +
                       0.125 * (old_grid(i, j + 1) + old_grid(i, j - 1) +
                                old_grid(i + 1, j) + old_grid(i - 1, j));
    }
  }
}
