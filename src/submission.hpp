#pragma once

#include <cstddef>
#include <cstdint>
#include <omp.h>
#include <sys/types.h>
#include <vector>

// x86-64-v3 has AVX2
const int SIMD_WIDTH = 256 / 8;
const int SIMD_ELEMENTS = SIMD_WIDTH / sizeof(double);

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to
// read results; it never touches your internal storage. Keep this
// interface, everything else is yours.
class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::size_t stride_;
  std::vector<double> data_;
  std::size_t offset_;

public:
  Grid(std::size_t rows, std::size_t cols)
      : rows_(rows), cols_(cols),
        stride_(((cols_ + SIMD_ELEMENTS - 1) / SIMD_ELEMENTS) * SIMD_ELEMENTS),
        data_(rows_ * stride_ + SIMD_ELEMENTS),
        offset_(lead_pad(data_.data())) {};

  static std::size_t lead_pad(const double *base) {
    uintptr_t a = reinterpret_cast<uintptr_t>(base) + sizeof(double);
    auto rounded_up = ((a + SIMD_WIDTH - 1) / (SIMD_WIDTH)) * (SIMD_WIDTH);
    return (rounded_up - a) / sizeof(double);
  }

  double &operator()(std::size_t i, std::size_t j) {
    return data_[offset_ + i * stride_ + j];
  };
  double operator()(std::size_t i, std::size_t j) const {
    return data_[offset_ + i * stride_ + j];
  };

  std::size_t rows() const { return rows_; }

  std::size_t cols() const { return cols_; }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.

// Inline since the docs state "The submission is a header, so anything with
// external linkage has to stay safe when more than one translation unit
// includes it"

// Restrict had no difference on the assembly - and it doesn't have a huge cost.
// The compiler generates two variations: one where it can SIMD, and one where
// the pointers overlap. It does an address comparison at runtime to figure out
// which one to dispatch. The overlapping pointer case should never happen -
// that would make the stencil kernel wrong, so it would never take the slow,
// non-SIMD path.
inline void apply_stencil(const Grid &old_grid, Grid &new_grid) {
  std::size_t rows = old_grid.rows();
  std::size_t cols = old_grid.cols();

  // Streams sequential in memory, SIMD
  for (std::size_t c = 0; c < cols; c++) {
    new_grid(0, c) = old_grid(0, c);
    new_grid(rows - 1, c) = old_grid(rows - 1, c);
  }

#pragma omp parallel for
  for (std::size_t r = 1; r < rows - 1; r++) {
    // Put this into the parllel region, since these cache lines are about to be
    // touched anyways.
    new_grid(r, 0) = old_grid(r, 0);
    for (std::size_t c = 1; c < cols - 1; c++) {
      new_grid(r, c) = 0.5 * old_grid(r, c) +
                       0.125 * (old_grid(r - 1, c) + old_grid(r + 1, c) +
                                old_grid(r, c - 1) + old_grid(r, c + 1));
    }
    new_grid(r, cols - 1) = old_grid(r, cols - 1);
  }
};
