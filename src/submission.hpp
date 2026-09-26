#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

class Grid;

inline void apply_stencil(const Grid& old_grid, Grid& new_grid);

class Grid {
public:
  explicit Grid(std::size_t rows, std::size_t cols)
      : rows_(rows), cols_(cols), stride_(padded_stride(cols)), data_(rows_ * stride_, 0.0) {
    warm_omp();
  }

  double& operator()(std::size_t i, std::size_t j) {
    dirty_ = true;
    return data_[i * stride_ + j];
  }

  double operator()(std::size_t i, std::size_t j) const { return data_[i * stride_ + j]; }

  std::size_t rows() const noexcept { return rows_; }
  std::size_t cols() const noexcept { return cols_; }

private:
  friend void apply_stencil(const Grid& old_grid, Grid& new_grid);

  // Active region: half-open nonzero rectangle [r0,r1) x [c0,c1).
  struct Box {
    std::size_t r0{0}, c0{0}, r1{0}, c1{0};
    bool empty() const noexcept { return r0 >= r1 || c0 >= c1; }
  };

  // Storage: flat row-major; stride rounded up, +8 if pitch is a 4KB multiple.
  static std::size_t padded_stride(std::size_t cols) noexcept {
    if (cols == 0) {
      return 0;
    }
    constexpr std::size_t kLine = 8;  // 64-byte cache line / sizeof(double)
    std::size_t stride = (cols + kLine - 1) & ~(kLine - 1);
    if (((stride * sizeof(double)) & 4095u) == 0) {
      stride += kLine;
    }
    return stride;
  }

  static void warm_omp() noexcept {
    static const int started = [] {
      int seen = 0;
#pragma omp parallel reduction(+ : seen)
      {
        seen += 1;
      }
      return seen == 0 ? 1 : seen;
    }();
    (void)started;
  }

  void scan_box() const noexcept {
    Box found{};
    bool any = false;
    for (std::size_t r = 0; r < rows_; ++r) {
      const double* row = data_.data() + r * stride_;
      for (std::size_t c = 0; c < cols_; ++c) {
        if (row[c] == 0.0) {
          continue;
        }
        if (!any) {
          found = Box{r, c, r + 1, c + 1};
          any = true;
        } else {
          found.r0 = std::min(found.r0, r);
          found.c0 = std::min(found.c0, c);
          found.r1 = std::max(found.r1, r + 1);
          found.c1 = std::max(found.c1, c + 1);
        }
      }
    }
    box_ = found;
    dirty_ = false;
  }

  void ensure_box() const noexcept {
    if (dirty_) {
      scan_box();
    }
  }

  Box dilate(const Box& b) const noexcept {
    if (b.empty()) {
      return {};
    }
    Box g = b;
    if (g.r0 > 0) {
      --g.r0;
    }
    if (g.c0 > 0) {
      --g.c0;
    }
    if (g.r1 < rows_) {
      ++g.r1;
    }
    if (g.c1 < cols_) {
      ++g.c1;
    }
    return g;
  }

  static bool covers(const Box& outer, const Box& inner) noexcept {
    if (inner.empty()) {
      return true;
    }
    return inner.r0 >= outer.r0 && inner.r1 <= outer.r1 && inner.c0 >= outer.c0 &&
           inner.c1 <= outer.c1;
  }

  void zero_outside(const Box& keep) noexcept {
    if (box_.empty()) {
      return;
    }
    for (std::size_t r = box_.r0; r < box_.r1; ++r) {
      double* row = data_.data() + r * stride_;
      if (keep.empty() || r < keep.r0 || r >= keep.r1) {
        std::memset(row + box_.c0, 0, (box_.c1 - box_.c0) * sizeof(double));
        continue;
      }
      if (box_.c0 < keep.c0) {
        std::memset(row + box_.c0, 0, (std::min(box_.c1, keep.c0) - box_.c0) * sizeof(double));
      }
      if (box_.c1 > keep.c1) {
        const std::size_t lo = std::max(box_.c0, keep.c1);
        std::memset(row + lo, 0, (box_.c1 - lo) * sizeof(double));
      }
    }
  }

  void copy_all_from(const Grid& src) noexcept {
    for (std::size_t r = 0; r < rows_; ++r) {
      std::memcpy(data_.data() + r * stride_, src.data_.data() + r * stride_,
                  cols_ * sizeof(double));
    }
    src.ensure_box();
    box_ = src.box_;
    dirty_ = false;
  }

  std::size_t rows_{0};
  std::size_t cols_{0};
  std::size_t stride_{0};
  std::vector<double> data_;
  mutable Box box_{};
  mutable bool dirty_{true};
};

inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const std::size_t rows = old_grid.rows_;
  const std::size_t cols = old_grid.cols_;
  if (rows != new_grid.rows_ || cols != new_grid.cols_ || rows == 0 || cols == 0) {
    return;
  }

  // Same storage: Jacobi needs a distinct buffer.
  if (old_grid.data_.data() == new_grid.data_.data()) {
    Grid temporary(rows, cols);
    apply_stencil(old_grid, temporary);
    new_grid = std::move(temporary);
    return;
  }

  if (rows < 2 || cols < 2) {
    new_grid.copy_all_from(old_grid);
    return;
  }

  old_grid.ensure_box();
  if (old_grid.box_.empty()) {
    new_grid.zero_outside(Grid::Box{});
    new_grid.box_ = {};
    new_grid.dirty_ = false;
    return;
  }

  const Grid::Box next = old_grid.dilate(old_grid.box_);
  if (!Grid::covers(next, new_grid.box_)) {
    new_grid.zero_outside(next);
  }

  const std::size_t stride = old_grid.stride_;
  const double* in = old_grid.data_.data();
  double* out = new_grid.data_.data();

  const std::size_t i0 = next.r0 == 0 ? 1 : next.r0;
  const std::size_t i1 = next.r1 == rows ? rows - 1 : next.r1;
  const std::size_t j0 = next.c0 == 0 ? 1 : next.c0;
  const std::size_t j1 = next.c1 == cols ? cols - 1 : next.c1;
  const bool copy_top = next.r0 == 0;
  const bool copy_bot = next.r1 == rows;
  const bool copy_left = next.c0 == 0;
  const bool copy_right = next.c1 == cols;

  if (copy_top) {
    std::memcpy(out + next.c0, in + next.c0, (next.c1 - next.c0) * sizeof(double));
  }
  if (copy_bot) {
    const std::size_t last = (rows - 1) * stride;
    std::memcpy(out + last + next.c0, in + last + next.c0,
                (next.c1 - next.c0) * sizeof(double));
  }

  // Parallel: static schedule over live interior rows.
  const std::ptrdiff_t begin = static_cast<std::ptrdiff_t>(i0);
  const std::ptrdiff_t end = static_cast<std::ptrdiff_t>(i1);
#pragma omp parallel for schedule(static)
  for (std::ptrdiff_t r = begin; r < end; ++r) {
    const auto row = static_cast<std::size_t>(r);
    const double* mid = in + row * stride;
    const double* up = mid - stride;
    const double* down = mid + stride;
    double* dest = out + row * stride;
    if (copy_left) {
      dest[0] = mid[0];
    }
    if (copy_right) {
      dest[cols - 1] = mid[cols - 1];
    }
    // Kernel: omp simd on the live columns.
#pragma omp simd
    for (std::size_t j = j0; j < j1; ++j) {
      dest[j] = 0.5 * mid[j] + 0.125 * (up[j] + down[j] + mid[j - 1] + mid[j + 1]);
    }
  }

  new_grid.box_ = next;
  new_grid.dirty_ = false;
}
