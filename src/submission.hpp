#pragma once

#include <cstddef>
#include <vector>
#include <cstring>
#include <algorithm>

struct ActiveBox {
  std::size_t top = 0;
  std::size_t bottom = 0;
  std::size_t left = 0;
  std::size_t right = 0;

  bool empty() const noexcept {
    return top >= bottom || left >= right;
  }
};

inline ActiveBox expand_box(ActiveBox box, std::size_t rows, std::size_t cols) {
  if (box.empty()) {return box;}
  if (box.top > 0) {--box.top;}
  if (box.bottom < rows) {++box.bottom;}
  if (box.left > 0) {--box.left;}
  if (box.right < cols) {++box.right;}

  return box;
}

class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> data_;

  bool boundaries_initialized_ = false;

  mutable ActiveBox active_;
  mutable bool active_known_ = true;

public:
  Grid(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols), data_(rows * cols, 0.0) {}

  double& operator()(std::size_t i, std::size_t j) {
    boundaries_initialized_ = false;
    active_known_ = false;
    return data_[i * cols_ + j];
  }
  double  operator()(std::size_t i, std::size_t j) const {
    return data_[i * cols_ + j];
  }

  std::size_t rows() const noexcept {
    return rows_;
  }

  std::size_t cols() const noexcept {
    return cols_;
  }

  const double* data() const noexcept {
    return data_.data();
  }

  double* data() noexcept {
    return data_.data();
  }

  bool boundaries_initialized() const noexcept {
    return boundaries_initialized_;
  }

  void set_boundaries_initialized(bool initialized) noexcept {
    boundaries_initialized_ = initialized;
  }

  bool active_known() const noexcept {
    return active_known_;
  }

  ActiveBox active_box() const {

    if (active_known_) {return active_;}

    bool found = false;

    ActiveBox box{};

    for (std::size_t i = 0; i < rows_; ++i) {
      const double* row = data_.data() + i * cols_;

      for (std::size_t j = 0; j < cols_; ++j) {
        if (row[j] == 0.0) {
          continue;
        }

        if (!found) {
          box = {i, i+1, j, j+1};

          found = true;
          continue;
        }

        box.top = std::min(box.top, i);
        box.bottom = std::max(box.bottom, i+1);
        box.left = std::min(box.left, j);
        box.right = std::max(box.right, j+1);
      }
    }

    if (!found) {box = {};}

    active_ = box;
    active_known_ = true;

    return active_;

  }

  void set_active_box(const ActiveBox& box) noexcept {
    active_ = box;
    active_known_ = true;
  }

};  

inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {

  constexpr std::size_t PARALLEL_MIN_CELLS = 65536;

  const std::size_t rows = old_grid.rows();
  const std::size_t cols = old_grid.cols();

  if (rows == 0 || cols == 0) {
    return;
  }

  const bool source_reinitialized = !old_grid.active_known();

  const ActiveBox old_box = old_grid.active_box();
  const ActiveBox next_box = expand_box(old_box, rows, cols);

  const double* __restrict__ input = old_grid.data();
  double* __restrict__ output = new_grid.data();

  if (source_reinitialized) {
    std::fill(output, output + rows * cols, 0.0);
    new_grid.set_active_box({});
    new_grid.set_boundaries_initialized(false);
  }

  if (!new_grid.boundaries_initialized()) {
    std::memcpy(output, input, cols * sizeof(double));

    if (rows > 1) {
      std::memcpy(output + (rows - 1) * cols, input + (rows - 1) * cols, cols * sizeof(double));
    }

    for (std::size_t i = 1; i + 1 < rows; ++i) {
      output[i * cols] = input[i * cols];
      
      if (cols > 1) {
        output[i * cols + cols - 1] = input[i * cols +  cols - 1];
      }
    }
    new_grid.set_boundaries_initialized(true);
  } 

  if (rows < 3 || cols < 3) {
    new_grid.set_active_box(next_box);
    return;
  }

  const std::size_t row_begin = std::max(next_box.top, std::size_t{1});
  const std::size_t row_end = std::min(next_box.bottom, rows - 1);
  const std::size_t col_begin = std::max(next_box.left, std::size_t{1});
  const std::size_t col_end = std::min(next_box.right, cols - 1);

  if (row_begin >= row_end || col_begin >= col_end) {
    new_grid.set_active_box(next_box);
    return;
  }

  const std::size_t active_cells = (row_end - row_begin) * (col_end - col_begin);



  #pragma omp parallel for schedule(static) if(active_cells >= PARALLEL_MIN_CELLS)
  for (std::size_t i = row_begin; i < row_end; ++i) {

    const double* top = input + (i - 1) * cols;
    const double* center = input + i * cols;
    const double* bottom = input + (i + 1) * cols;

    double* out = output + i * cols;

    #pragma omp simd
    for (std::size_t j = col_begin; j < col_end; ++j) {
      out[j] = 0.125 * ((4 * center[j]) + top[j] + bottom[j] + center[j - 1] + center[j + 1]);
    }
  }

  new_grid.set_active_box(next_box);

}


