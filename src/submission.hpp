#pragma once

#include <cstddef>
#include <tuple>
#include <vector>
#include <algorithm>

// 2D grid of doubles stored as a single row-major buffer.
class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;

  std::vector<double> nodes;

public:
  Grid(std::size_t rows, std::size_t cols);

  // Returns (rows, cols).
  std::tuple<std::size_t, std::size_t> get_dimensions() const;

  // Row-major element access: nodes[i * cols_ + j].
  double &operator()(std::size_t i, std::size_t j);
  double operator()(std::size_t i, std::size_t j) const;

  // Raw access to the underlying buffer, e.g. for pointer-based/SIMD code.
  double *data() { return nodes.data(); }
  const double *data() const { return nodes.data(); }

  // Copies the outer border (row 0, row rows_-1, col 0, col cols_-1) from
  // ref into this grid, leaving the interior untouched.
  void copy_boundary(const Grid &ref);
};

inline Grid::Grid(std::size_t rows, std::size_t cols)
    : nodes(rows * cols), rows_(rows), cols_(cols) {}

inline std::tuple<std::size_t, std::size_t> Grid::get_dimensions() const {
  return std::tuple<std::size_t, std::size_t>(rows_, cols_);
}

inline double &Grid::operator()(std::size_t i, std::size_t j) {
  return nodes[i * cols_ + j];
}

inline double Grid::operator()(std::size_t i, std::size_t j) const {
  return nodes[i * cols_ + j];
}

inline void Grid::copy_boundary(const Grid &ref) {
  #pragma omp parallel
  {
    #pragma omp for
    for (std::size_t i = 0; i < cols_; ++i) {
      nodes[i] = ref(0, i);
      nodes[(rows_ - 1) * cols_ + i] = ref(rows_ - 1, i);
    }

    #pragma omp for
    for (std::size_t i = 1; i < rows_ - 1; ++i) {
      nodes[i * cols_] = ref(i, 0);
      nodes[i * cols_ + cols_ - 1] = ref(i, cols_ - 1);
    }
  }
}

// Computes new_grid from old_grid using a 5-point stencil over the
// interior, and copies old_grid's boundary into new_grid unchanged.
inline void apply_stencil(const Grid &old_grid, Grid &new_grid) {
  new_grid.copy_boundary(old_grid);
  auto [rows, columns] = old_grid.get_dimensions();

  if (rows < 3 || columns < 3) return;

  const double *__restrict src = old_grid.data();
  double *__restrict dst = new_grid.data();

  const std::size_t tile_size = 64;
  const std::size_t tile_rows_count = (rows - 2 + tile_size - 1) / tile_size;
  const std::size_t tile_columns_count = (columns - 2 + tile_size - 1) / tile_size;

  // Grid cell traversal.
  #pragma omp parallel for schedule(static)
  for (std::size_t tile_row = 0; tile_row < tile_rows_count; ++tile_row) {
    for (std::size_t tile_column = 0; tile_column < tile_columns_count; ++tile_column) {

      const std::size_t row_begin = 1 + tile_row * tile_size;
      const std::size_t row_end = std::min(row_begin + tile_size, rows - 1);

      const std::size_t column_begin = 1 + tile_column * tile_size;
      const std::size_t column_end = std::min(column_begin + tile_size, columns - 1);

      // Interior cell traversal.
      for (std::size_t i = row_begin; i < row_end; ++i) {
        const double *__restrict srow = src + i * columns;
        const double *__restrict srowU = src + (i - 1) * columns;
        const double *__restrict srowD = src + (i + 1) * columns;
        double *__restrict drow = dst + i * columns;

        #pragma omp simd
        for (std::size_t j = column_begin; j < column_end; ++j) {
          drow[j] = 0.5 * srow[j] +
                0.125 * (srowU[j] + srowD[j] + srow[j - 1] + srow[j + 1]);
        }
      }
    }
  }
}
