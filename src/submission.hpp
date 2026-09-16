#pragma once

#include <cstddef>
#include <cstring>
#include <vector>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
struct ConstGridView {
  const double* data;
  std::size_t rows;
  std::size_t cols;
  std::size_t stride;

  double operator()(std::size_t i, std::size_t j) const {
    return data[i * stride + j];
  }
};

struct GridView {
  double* data;
  std::size_t rows;
  std::size_t cols;
  std::size_t stride;

  double& operator()(std::size_t i, std::size_t j) {
    return data[i * stride + j];
  }
};

class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> data_;

public:
  Grid(std::size_t rows, std::size_t cols)
    : rows_(rows), cols_(cols), data_(rows * cols) {
  }

  double& operator()(std::size_t i, std::size_t j) {
    return data_[i * cols_ + j];
  }

  double operator()(std::size_t i, std::size_t j) const {
    return data_[i * cols_ + j];
  }

  std::size_t rows() const {
    return rows_;
  }

  std::size_t cols() const {
    return cols_;
  }

  ConstGridView view() const {
    return {
      data_.data(),
      rows_,
      cols_,
      cols_
    };
  }

  GridView view() {
    return {
      data_.data(),
      rows_,
      cols_,
      cols_
    };
  }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const ConstGridView old_view = old_grid.view();
  GridView new_view = new_grid.view();

  const std::size_t rows = old_view.rows;
  const std::size_t cols = old_view.cols;

  if (rows == 0 || cols == 0) {
    return;
  }

  const double* __restrict old_data = old_view.data;
  double* __restrict new_data = new_view.data;

  if (rows < 3 || cols < 3) {
    for (std::size_t i = 0; i < rows; ++i) {
      for (std::size_t j = 0; j < cols; ++j) {
        new_view(i, j) = old_view(i, j);
      }
    }
    return;
  }

  std::memcpy(
    new_data,
    old_data,
    cols * sizeof(double)
  );

  std::memcpy(
    new_data + (rows - 1) * old_view.stride,
    old_data + (rows - 1) * old_view.stride,
    cols * sizeof(double)
  );

  #pragma omp parallel for schedule(static)
  for (std::size_t i = 1; i < rows - 1; ++i) {
    const double* top = old_data + (i - 1) * old_view.stride;
    const double* mid = old_data + i * old_view.stride;
    const double* bottom = old_data + (i + 1) * old_view.stride;
    double* out = new_data + i * new_view.stride;

    out[0] = mid[0];
    out[cols - 1] = mid[cols - 1];

    #pragma omp simd
    for (std::size_t j = 1; j < cols - 1; ++j) {
      out[j] =
          0.5 * mid[j] +
          0.125 * (
              top[j] +
              bottom[j] +
              mid[j - 1] +
              mid[j + 1]
          );
    }
  }
}