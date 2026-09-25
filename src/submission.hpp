#pragma once
#include <vector>
#include <cstddef>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
  static constexpr std::size_t kAlignWidth = 4;

  std::size_t rows_;
  std::size_t cols_;
  std::size_t stride_;
  std::vector<double> data_;

public:
  Grid(std::size_t rows, std::size_t cols) 
  : rows_(rows), cols_(cols), 
  stride_(((cols_ + kAlignWidth - 1) / kAlignWidth) * kAlignWidth),
  data_(rows_ * stride_, 0.0)
  { }

  double& operator()(std::size_t i, std::size_t j) {
    return data_[i * stride_ + j];
  }

  double  operator()(std::size_t i, std::size_t j) const {
    return data_[i * stride_ + j];
  }

  double* data() { return data_.data(); }
  const double* data() const { return data_.data(); }

  std::size_t get_rows() const { return rows_; }
  std::size_t get_cols() const { return cols_; }
  std::size_t get_stride() const { return stride_; }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  std::size_t rows = old_grid.get_rows();
  std::size_t cols = old_grid.get_cols();
  std::size_t stride = old_grid.get_stride();

  const double* __restrict in = old_grid.data();
  double* __restrict out = new_grid.data();

  const std::ptrdiff_t row_end = static_cast<std::ptrdiff_t>(rows) - 1;
  #pragma omp parallel for schedule(static)
  for(std::ptrdiff_t i = 1; i < row_end; i++) {

    const double* row_up = in + (i - 1) * stride;
    const double* row_center = in + i * stride;
    const double* row_down = in + (i + 1) * stride;
    double* out_row = out + i * stride;

    for(std::size_t j = 1; j  + 1 < cols; j++) {
      out_row[j] = 0.5 * row_center[j] +
      0.125 * (row_up[j] + row_down[j] + row_center[j - 1] + row_center[j + 1]);
    }
  }

  if(rows > 0) {
    for (std::size_t j = 0; j < cols; j++) {
      new_grid(0, j) = old_grid(0, j);
      new_grid(rows - 1, j) = old_grid(rows - 1, j);
    }
  }

  if(cols > 0) {
    for (std::size_t i = 0; i < rows; i++) {
      new_grid(i, 0) = old_grid(i, 0);
      new_grid(i, cols - 1) = old_grid(i, cols - 1);
    }
  }
}
