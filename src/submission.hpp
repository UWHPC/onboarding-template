#pragma once

#include <cstddef>
#include <memory>
#include <new>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
  static constexpr std::size_t lineLength = 64;                               // number of bytes in a cache line
  static constexpr std::size_t doublesPerLine = lineLength / sizeof(double);  // 8 doubles


  struct AlignedDelete {
    void operator()(double* p) const noexcept {
      ::operator delete(p, std::align_val_t{lineLength});
    }
  };

  std::size_t rows_;
  std::size_t cols_;
  std::size_t stride_;
  std::unique_ptr<double[], AlignedDelete> owned_;
  double* base_;    

public:
  Grid(std::size_t rows, std::size_t cols)
    : rows_(rows), cols_(cols)
  {
    stride_ = (cols + doublesPerLine - 1) / doublesPerLine * doublesPerLine;
    
    // One spare line so base_ can be shifted forward without overrunning.
    const std::size_t total = rows_ * stride_ + doublesPerLine;
    owned_.reset(static_cast<double*>(
      ::operator new(total * sizeof(double), std::align_val_t{lineLength})));

    // apply_stencil's loop starts at column 1, so we want the row[1] and not row[0]
    // to be cache-line aligned. Shifting the base_ by doublesPerLine - 1 pushes
    // the 2nd element of each row to be at the start of each cache line
    base_ = owned_.get() + (doublesPerLine - 1);

    for (std::size_t k = 0; k < total; ++k) owned_[k] = 0.0;
  }

  double& operator()(std::size_t i, std::size_t j)       { return base_[i * stride_ + j]; }
  double  operator()(std::size_t i, std::size_t j) const { return base_[i * stride_ + j]; }

  std::size_t rows() const { return rows_; }
  std::size_t cols() const { return cols_; }

  // Row pitch in doubles. Not the same as cols() once padding is applied.
  std::size_t stride() const { return stride_; }

  // raw data access for apply_stencil. This exists so that apply_stencil has a ptr
  // it can use with __restrict, as putting __restrict on the Grid& params only
  // promises the two Grid objects don't overlap and it says nothing about the heap
  // buffers they own.
  double*       data()       { return base_; }
  const double* data() const { return base_; }

};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
inline void apply_stencil(const Grid& old_grid, Grid& new_grid){
    const std::size_t m{old_grid.rows()}, n{old_grid.cols()};

    //invalid dimension
    if (m == 0 || n == 0) return;

    // __restrict promises that the read and write data do not overlap. Without it
    // the compiler assumes a store to new_grid might clobber a neighbour it is about
    // to read from old_grid, and guards the vector loop with a runtime check
    const std::size_t stride{old_grid.stride()};
    const double* __restrict src{old_grid.data()};
    double* __restrict dst{new_grid.data()};

    // processing boundary rows first to avoid conditional checks for each iteration of double forloop
    // top and bottom rows
    const std::size_t bottom{(m - 1) * stride};
    for (std::size_t j = 0; j < n; ++j) {
        dst[j] = src[j];
        dst[bottom + j] = src[bottom + j];
    }

    // Left/right columns are folded into the interior loop rather than done in a
    // separate pass. A separate pass is worse for performance (i think) as it accesses new cache line
    // per row
    #pragma omp parallel for schedule(static)
    for (std::size_t i = 1; i < m - 1; ++i) {
        const double* __restrict up{src + (i - 1) * stride};
        const double* __restrict ctr{src + i * stride};
        const double* __restrict down{src + (i + 1) * stride};
        double* __restrict out{dst + i * stride};

        out[0] = ctr[0];
        #pragma omp simd
        for (std::size_t j = 1; j < n - 1; ++j) {
            out[j] = 0.5 * ctr[j] +
                     0.125 * (up[j] + down[j] +
                              ctr[j - 1] + ctr[j + 1]);
        }
        out[n - 1] = ctr[n - 1];
    }
}
