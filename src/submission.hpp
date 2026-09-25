#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>
#include <utility>

#if defined(__GNUC__) || defined(__clang__)
#define UWHPC_RESTRICT __restrict__
#else
#define UWHPC_RESTRICT
#endif

class Grid {
private:
  static constexpr std::size_t kAlignment{64};
  static constexpr std::size_t kCachePageBytes{4096};

  std::size_t rows_;
  std::size_t cols_;
  std::size_t stride_;
  double* data_;

  static std::size_t choose_stride(std::size_t cols) {
    if (cols == 0) return 0;
    for (std::size_t pad{0}; pad < 8; ++pad) {
      if (((cols + pad) * sizeof(double)) % kCachePageBytes != 0) return cols + pad;
    }
    return cols + 1;
  }

public:
  Grid(std::size_t rows, std::size_t cols):
        rows_{rows},
        cols_{cols},
        stride_{choose_stride(cols)},
        data_{nullptr} {
    if (rows_ == 0 || cols_ == 0) {
      rows_ = 0;
      cols_ = 0;
      stride_ = 0;
      return;
    }



    data_ = static_cast<double*>(::operator new(stride_ * rows_ * sizeof(double), std::align_val_t(kAlignment)));

    std::fill(data_, data_ + stride_ * rows_, 0.0);
  }

  ~Grid() {
    ::operator delete(data_, std::align_val_t(kAlignment));
  }

  Grid(const Grid&) = delete;
  Grid& operator=(const Grid&) = delete;

  Grid(Grid&& other) noexcept
      : rows_{other.rows_}, cols_{other.cols_}, stride_{other.stride_}, data_{other.data_} {
    other.rows_ = 0;
    other.cols_ = 0;
    other.stride_ = 0;
    other.data_ = nullptr;
  }

  Grid& operator=(Grid&& other) noexcept {
    if (this != &other) {
      ::operator delete(data_, std::align_val_t(kAlignment));
      rows_ = other.rows_;
      cols_ = other.cols_;
      stride_ = other.stride_;
      data_ = other.data_;
      other.rows_ = 0;
      other.cols_ = 0;
      other.stride_ = 0;
      other.data_ = nullptr;
    }
    return *this;
  }

  double& operator()(std::size_t i, std::size_t j) { return data_[i * stride_ + j]; }
  double operator()(std::size_t i, std::size_t j) const { return data_[i * stride_ + j]; }

  std::size_t rows() const { return rows_; }
  std::size_t cols() const { return cols_; }
  std::size_t stride() const { return stride_; }

  double* row(std::size_t i) { return data_ + i * stride_; }
  const double* row(std::size_t i) const { return data_ + i * stride_; }
};


void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const std::size_t rows{old_grid.rows()};
  const std::size_t cols{old_grid.cols()};

  if (rows < 3 || cols < 3) {
    for (std::size_t i{0}; i < rows; ++i) {
      std::memcpy(new_grid.row(i), old_grid.row(i), cols * sizeof(double));
    }
    return;
  }

  std::memcpy(new_grid.row(0), old_grid.row(0), cols * sizeof(double));
  std::memcpy(new_grid.row(rows - 1), old_grid.row(rows - 1), cols * sizeof(double));

  const std::ptrdiff_t interior_end{static_cast<std::ptrdiff_t>(rows) - 1};
#pragma omp parallel for schedule(static)
  for (std::ptrdiff_t k = 1; k < interior_end; ++k) {
    const std::size_t i{static_cast<std::size_t>(k)};

    const double* UWHPC_RESTRICT above{old_grid.row(i - 1)};
    const double* UWHPC_RESTRICT mid{old_grid.row(i)};
    const double* UWHPC_RESTRICT below{old_grid.row(i + 1)};
    double* UWHPC_RESTRICT out{new_grid.row(i)};
    out[0] = mid[0];
    out[cols - 1] = mid[cols - 1];

    for (std::size_t j{1}; j + 1 < cols; ++j) {
      out[j] = 0.5 * mid[j] +
               0.125 * (above[j] + below[j] + mid[j - 1] + mid[j + 1]);
    }
  }
}
