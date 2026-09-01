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
  
  const double *__restrict src_add = old_grid.data();
  double *__restrict dst_add = new_grid.data();

  std::memcpy(dst_add, src_add, cols * sizeof(double));
  std::memcpy((dst_add + (cols * (rows - 1))), 
              (src_add + (cols * (rows - 1))), 
              cols * sizeof(double));

#pragma omp parallel for schedule(static)
  for (std::size_t i = 1; i < rows - 1; ++i) {

    const double* up = src_add + ((i-1) * cols);  
    const double* mid = src_add + (i * cols);  
    const double* down = src_add + ((i+1) * cols);  
    double* write_add = dst_add + (i * cols);

    // copy first and last value of each row
    write_add[0] = mid[0];
    write_add[cols - 1] = mid[cols - 1];

    #pragma omp simd
    for (std::size_t j = 1; j < cols - 1; ++j) {
      write_add[j] = 0.5 * mid[j] + 0.125 * (mid[j+1] + mid[j-1] + up[j] + down[j]);
    }
  }
}
