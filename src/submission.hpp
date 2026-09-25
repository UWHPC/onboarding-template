#pragma once

#include <algorithm>
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
  std::size_t stride_;
  std::vector<double> data_;

public:
  Grid(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols), stride_(((cols + 7) / 8 + 1) * 8), data_(rows * stride_, 0) {};

  std::size_t rows() const {
    return rows_;
  };
  std::size_t cols() const {
    return cols_;
  };
  std::size_t stride() const {
    return stride_;
  }

  double* data() {
    return data_.data();
  };
  const double* data() const {
    return data_.data();
  };

  double& operator()(std::size_t i, std::size_t j) {
    return data_[i * stride_ + j];
  };
  double  operator()(std::size_t i, std::size_t j) const {
    return data_[i * stride_ + j];
  };
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const std::size_t rows = old_grid.rows();
  const std::size_t cols = old_grid.cols();
  const std::size_t stride = old_grid.stride();

  const double* old_data = old_grid.data();
  double* new_data = new_grid.data();

  if (rows == 0 || cols == 0) {
    return;
  }

#pragma omp parallel for schedule(static)
  for (std::size_t i = 1; i < rows - 1; i++) {
    const double* current = old_data + i * stride;
    const double* top = current - stride;
    const double* bottom = current + stride;

    double* out = new_data + i * stride;

#pragma omp simd
    for (std::size_t j = 1; j < cols - 1; j++) {
      out[j] = 0.5 * current[j] + 0.125 * (top[j] + bottom[j] + current[j - 1] + current[j + 1]);
    }

    out[0] = current[0];
    out[cols - 1] = current[cols - 1];
  }

  const std::size_t end = stride * (rows - 1);
  std::copy(old_data, old_data + cols, new_data);
  std::copy(old_data + end, old_data + end + cols, new_data + end);
};
