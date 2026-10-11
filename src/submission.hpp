#pragma once

#include <cstddef>
#include <tuple>
#include <vector>
#include <immintrin.h>

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

inline double calculate(const double c, const double u, const double d, const double l, const double r) {
  return 0.5 * c + 0.125 * (u + d + l + r);
}

// Computes new_grid from old_grid using a 5-point stencil over the
// interior, and copies old_grid's boundary into new_grid unchanged.
inline void apply_stencil(const Grid &old_grid, Grid &new_grid) {
  auto [rows, cols] = old_grid.get_dimensions();
  new_grid.copy_boundary(old_grid);

  if (rows < 3 || cols < 3)
    return;

  // __restrict promises src/dst don't alias, letting the compiler
  // vectorize the inner loop.
  const double *__restrict src = old_grid.data();
  double *__restrict dst = new_grid.data();

  // Constants.
  __m256d half = _mm256_set1_pd(0.5);
  __m256d quart = _mm256_set1_pd(0.125);

  // Interior rows are independent, so parallelize across them.
  #pragma omp parallel for
  for (std::size_t i = 1; i < rows - 1; ++i) {
    const double *__restrict srow = src + i * cols;
    const double *__restrict srowU = src + (i - 1) * cols;
    const double *__restrict srowD = src + (i + 1) * cols;
    double *__restrict drow = dst + i * cols;

    std::size_t j = 1;
    
    // Hand-vectorized with AVX2: each iteration computes 4 outputs at once.
    // This cuts the instruction count (and compute work) per element, but the
    // stencil is memory-bandwidth bound: each output still needs the same bytes
    // loaded and stored, so the original benchmark was already limited by
    // memory traffic, not ALU throughput.
    for (; j + 4 < cols; j += 4) {

      // Loads.
      __m256d prev = _mm256_loadu_pd(srow - 1 + j);
      __m256d curr = _mm256_loadu_pd(srow + j);
      __m256d next = _mm256_loadu_pd(srow + 1 + j);
      __m256d up = _mm256_loadu_pd(srowU + j); 
      __m256d down = _mm256_loadu_pd(srowD + j); 

      // Calculate sum or adjacent tiles.
      __m256d adj = _mm256_add_pd(_mm256_add_pd(_mm256_add_pd(up, down), prev), next);

      // Calculate 0.5 * curr.
      __m256d nc = _mm256_mul_pd(curr, half);

      // Multiply adjacent sums by 0.125 and add to 0.5 * curr.
      //__m256d res = _mm256_fmadd_pd(adj, quart, nc);
      __m256d scaled = _mm256_mul_pd(adj, quart);
      __m256d res = _mm256_add_pd(scaled, nc);

      // Store in destination buffer.
      _mm256_storeu_pd(drow + j, res);
    }

    for (; j < cols - 1; ++j) {
      drow[j] = calculate(srow[j], srowU[j], srowD[j], srow[j - 1], srow[j + 1]);
    }
  }
}
