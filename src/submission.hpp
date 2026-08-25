#pragma once

#include <vector>
#include <cstddef>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> data_;
public:
  Grid(std::size_t rows, std::size_t cols): rows_(rows), cols_(cols), data_(rows * cols, 0.0){}

  double& operator()(std::size_t i, std::size_t j){ 
      return data_[i * cols_ + j];
  }

  double operator()(std::size_t i, std::size_t j) const { 
      return data_[i * cols_ + j];
  }

  std::size_t rows() const { 
      return rows_ ; 
  }

  std::size_t cols() const { 
      return cols_ ; 
  }

  // raw data access for apply_stencil. This exists so that apply_stencil has a ptr
  // it can use with __restrict, as putting __restrict on the Grid& params only
  // promises the two Grid objects don't overlap and it says nothing about the heap
  // buffers their vectors own. 
  double* data() { 
      return data_.data();
  }

  const double* data() const { 
      return data_.data();
  }
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
    const double* __restrict src{old_grid.data()};
    double* __restrict dst{new_grid.data()};

    // processing boundary rows first to avoid conditional checks for each iteration of double forloop
    // top and bottom rows
    const std::size_t bottom{(m - 1) * n};
    for (std::size_t j = 0; j < n; ++j) {
        dst[j] = src[j];
        dst[bottom + j] = src[bottom + j];
    }

    // Left/right columns are folded into the interior loop rather than done in a
    // separate pass. A separate pass is worse for performance (i think) as it accesses new cache line per row
    #pragma omp parallel for schedule(static)
    for (std::size_t i = 1; i < m - 1; ++i) {
        // Hoisted once per row: the inner loop then walks four contiguous
        // streams instead of recomputing i*cols_ for every one of the five taps.
        const double* __restrict up{src + (i - 1) * n};
        const double* __restrict ctr{src + i * n};
        const double* __restrict down{src + (i + 1) * n};
        double* __restrict out{dst + i * n};

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
