#pragma once

#include <cstddef>
#include <vector>

namespace stencil_detail {
  struct Box {
    std::size_t row_begin{0}, row_end{0}, col_begin{0}, col_end{0};
    bool empty() const { return row_begin >= row_end || col_begin >= col_end; }
  };
}

class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> data_;
  mutable stencil_detail::Box box_{};
  mutable bool box_known_{true};

public:
  Grid(std::size_t rows, std::size_t cols) : rows_{rows}, cols_{cols}, data_(rows * cols, 0.0) {}

  std::size_t rows() const { return rows_; }
  std::size_t cols() const { return cols_; }

  const stencil_detail::Box& box() const { return box_; }
  bool box_known() const { return box_known_; }
  void set_box(const stencil_detail::Box& box) const { box_ = box; box_known_ = true; }

  double& operator()(std::size_t i, std::size_t j) {
    box_known_ = false;
    return data_[i * cols_ + j];
  }
  double operator()(std::size_t i, std::size_t j) const { return data_[i * cols_ + j]; }
};

namespace stencil_detail {
  inline Box scan_support(const Grid& grid) {
    const std::size_t rows = grid.rows(), cols = grid.cols();
    Box box{rows, 0, cols, 0};

    for (std::size_t i = 0; i < rows; ++i) {
      std::size_t first = cols;

      for (std::size_t j = 0; j < cols; ++j) {
        if (grid(i, j) != 0.0) { first = j; break; }
      }

      if (first == cols) continue;
      
      std::size_t last = first;

      for (std::size_t j = cols; j > first; --j) {
        if (grid(i, j - 1) != 0.0) { last = j - 1; break; }
      }

      if (i < box.row_begin) box.row_begin = i;

      box.row_end = i + 1;
      
      if (first < box.col_begin) box.col_begin = first;
      if (last + 1 > box.col_end) box.col_end = last + 1;
    }

    if (box.row_begin >= box.row_end || box.col_begin >= box.col_end) return Box{0, 0, 0, 0};

    return box;
  }

  inline Box grow(const Box& box, const std::size_t rows, const std::size_t cols) {
    if (box.empty()) return Box{0, 0, 0, 0};

    Box b;
    b.row_begin = box.row_begin > 0 ? box.row_begin - 1 : 0;
    b.row_end = box.row_end + 1 < rows ? box.row_end + 1 : rows;
    b.col_begin = box.col_begin > 0 ? box.col_begin - 1 : 0;
    b.col_end = box.col_end + 1 < cols ? box.col_end + 1 : cols;

    return b;
  }
}

void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  using namespace stencil_detail;

  const std::size_t rows = old_grid.rows();
  const std::size_t cols = old_grid.cols();
  if (rows == 0 || cols == 0) return;

  Box active;
  if (old_grid.box_known()) {
    active = grow(old_grid.box(), rows, cols);
  } else {
    old_grid.set_box(scan_support(old_grid));
    active = Box{0, rows, 0, cols};
  }
  const Box result_box = grow(old_grid.box(), rows, cols);
  if (active.empty()) {
    new_grid.set_box(result_box);
    return;
  }

  const std::size_t inner_begin = active.col_begin > 1 ? active.col_begin : 1;
  const std::size_t inner_end = active.col_end + 1 < cols ? active.col_end : (cols > 0 ? cols - 1 : 0);

  for (std::size_t i = active.row_begin; i < active.row_end; ++i) {
    if (i == 0 || i + 1 == rows) {
      for (std::size_t j = active.col_begin; j < active.col_end; ++j) new_grid(i, j) = old_grid(i, j);
      continue;
    }
    if (active.col_begin == 0) new_grid(i, 0) = old_grid(i, 0);
    if (active.col_end == cols && cols > 1) new_grid(i, cols - 1) = old_grid(i, cols - 1);

    for (std::size_t j = inner_begin; j < inner_end; ++j) {
      new_grid(i, j) = 0.5 * old_grid(i, j) +
                       0.125 * (old_grid(i - 1, j) + old_grid(i + 1, j) +
                                old_grid(i, j - 1) + old_grid(i, j + 1));
    }
  }

  new_grid.set_box(result_box);
}
