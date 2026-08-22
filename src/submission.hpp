#pragma once

#include "immintrin.h"
#include <assert.h>
#include <bits/stdc++.h> // oh how i missed you...
using namespace std;

const size_t N = sizeof(__m256d) / sizeof(double);
static size_t simd_size(size_t n) { return (n + N - 1) / N; }

// #define COLIN_ASSERT(x) assert(x)
#define COLIN_ASSERT(x)

class Grid {
public:
  // invariant: rows >= cols
  // (the grid is stored in a row-major order so having a large amount of rows
  // makes multithreading more efficent) (well, that's the theory anyways)
  size_t rows;
  size_t cols;

  size_t stride;

  __m256d *grid;
  bool transposed;

  Grid(size_t r, size_t c) : transposed(r < c) {
    if (transposed)
      std::swap(r, c);
    rows = r;
    cols = c;

    stride = simd_size(cols);
    grid = new __m256d[rows * stride];
  }

  inline double &operator()(size_t i, size_t j) {
    if (transposed)
      swap(i, j);
    return ((double *)row(i))[j];
  }
  inline double operator()(size_t i, size_t j) const {
    if (transposed)
      swap(i, j);
    return ((double *)row(i))[j];
  }

  inline __m256d *row(size_t i) const { return &grid[i * stride]; }
};

static inline void apply_stencil_cold(const double *dx, double *dy, size_t j,
                                      size_t stride) {
  stride *= N;
  auto x = &dx[j];
  dy[j] = 0.125 * (x[-1] + x[1] + x[-stride] + x[stride]) + 0.5 * x[0];
}

static void apply_stencil(const Grid &x, Grid &y) {

  // thanks to the nice property that the kernel is rotationally symmetric, we
  // don't actually have to care whether the grid is transposed or not!
  const auto [rows, cols, stride, _, _2] = x;
  COLIN_ASSERT(rows == y.rows && cols == y.cols && stride == y.stride);

  {
    auto rx0 = x.row(0), ry0 = y.row(0);
    auto rx1 = x.row(rows - 1), ry1 = y.row(rows - 1);

#pragma omp parallel for
    for (size_t i = 0; i < stride; i++) {
      ry0[i] = rx0[i];
      ry1[i] = rx1[i];
    }
  }
  size_t hot_start = N;
  size_t hot_end = (cols - 1) / N * N;
  if (hot_end <= hot_start) {
    // no hot loop iterations; do slow loop only
    hot_start = hot_end = cols - 1;
  }
#pragma omp parallel for
  for (size_t i = 1; i < rows - 1; i++) {
    auto rx = x.row(i), ry = y.row(i);
    auto dx = (double *)rx, dy = (double *)ry;
    dy[0] = dx[0];
    dy[cols - 1] = dx[cols - 1];

    for (size_t j = 1; j < hot_start; j++) {
      apply_stencil_cold(dx, dy, j, stride);
    }
    if (hot_end <= hot_start)
      continue;

    for (size_t j = hot_end; j < cols - 1; j++) {
      apply_stencil_cold(dx, dy, j, stride);
    }

    COLIN_ASSERT(hot_start == N);

    auto rx_m1 = x.row(i - 1);
    auto rx_p1 = x.row(i + 1);

    auto n_half = _mm256_set1_pd(0.5);
    auto n_eighth = _mm256_set1_pd(0.125);

    for (size_t s = hot_start / N; s < hot_end / N; s++) {
      COLIN_ASSERT(s + 1 < stride);
      auto c0 = rx[s];
      auto c1 = _mm256_loadu_pd((double *)&rx[s] - 1);
      auto c2 = _mm256_loadu_pd((double *)&rx[s] + 1);
      auto c3 = rx_m1[s];
      auto c4 = rx_p1[s];
      auto s0 = _mm256_mul_pd(c0, n_half);
      auto s1 = _mm256_add_pd(_mm256_add_pd(c1, c3), _mm256_add_pd(c2, c4));

      ry[s] = _mm256_fmadd_pd(s1, n_eighth, s0);
    }
  }
}
