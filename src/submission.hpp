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
      : rows_(rows), cols_(cols), data_(rows * cols, 0.0) {}

  double& operator()(std::size_t i, std::size_t j) {
    return data_[i * cols_ + j];
  }
  double operator()(std::size_t i, std::size_t j) const {
    return data_[i * cols_ + j];
  }

  std::size_t rows() const { return rows_; }
  std::size_t cols() const { return cols_; }

  double*       data()       { return data_.data(); }
  const double* data() const { return data_.data(); }
};

// ---------------------------------------------------------------------------

inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const std::size_t rows = old_grid.rows();
  const std::size_t cols = old_grid.cols();

  const double* __restrict old_data = old_grid.data();
  double*       __restrict new_data = new_grid.data();

  // boundary wholesale
  
  if (rows < 3 || cols < 3) {
    std::memcpy(new_data, old_data, rows * cols * sizeof(double));
    return;
  }

  // Top and bottom boundary rows.
  std::memcpy(new_data, old_data, cols * sizeof(double));
  std::memcpy(new_data + (rows - 1) * cols,
              old_data + (rows - 1) * cols,
              cols * sizeof(double));

  for (std::size_t i = 1; i + 1 < rows; ++i) {
    const double* __restrict up   = old_data + (i - 1) * cols;
    const double* __restrict cur  = old_data + (i)     * cols;
    const double* __restrict down = old_data + (i + 1) * cols;
    double*       __restrict out  = new_data + (i)     * cols;

    out[0] = cur[0];  // left boundary column

    for (std::size_t j = 1; j + 1 < cols; ++j) {
      out[j] = 0.5   * cur[j]
             + 0.125 * (up[j] + down[j] + cur[j - 1] + cur[j + 1]);
    }

    out[cols - 1] = cur[cols - 1];  // right boundary column
  }
}
