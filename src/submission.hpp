#pragma once

#include <cstddef>
#include <vector>
#include <algorithm>
#include <new>
#include <array>

inline constexpr std::size_t cache_line_size = 64;
inline constexpr std::size_t cache_line_doubles = cache_line_size / sizeof(double);
inline constexpr std::size_t simd_doubles = 4; // AVX2: 32-byte grab / 8-byte double

/* padding the row length only guarantees that rows sit at a uniform offset from the
start of the allocation. if that start is not itself divisible by 64, every row is off
by the same amount and no row begins at a cache line boundary. this allocator fixes the
start, and std::vector still owns and frees the memory so no cleanup work moves to Grid */
template <typename T, std::size_t Alignment>
struct AlignedAllocator {
  using value_type = T;

  AlignedAllocator() noexcept = default;

  template <typename U>
  AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

  // std::vector cannot derive this itself because Alignment is not a type parameter
  template <typename U>
  struct rebind {
    using other = AlignedAllocator<U, Alignment>;
  };

  T* allocate(std::size_t n) {
    return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{Alignment}));
  }

  void deallocate(T* p, std::size_t) noexcept {
    ::operator delete(p, std::align_val_t{Alignment});
  }
};

// two of these allocators are interchangeable whenever their alignment matches
template <typename T, typename U, std::size_t A>
bool operator==(const AlignedAllocator<T, A>&, const AlignedAllocator<U, A>&) noexcept {
  return true;
}

template <typename T, typename U, std::size_t A>
bool operator!=(const AlignedAllocator<T, A>&, const AlignedAllocator<U, A>&) noexcept {
  return false;
}

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
  std::array<std::size_t, 2> extents_;
  std::size_t stride_;
  std::vector<double, AlignedAllocator<double, cache_line_size>> cells_;

public:
  // round the size of a row up to the nearest multiple of the cache line size in doubles (8) so that each row starts at the beginning of each cache line
  // i didn't see any speedup by rounding stride to a non-power of 2, multiple of 8 (e.g. 1032). likely that each set holds more than 2 cache lines
  Grid(std::size_t rows, std::size_t cols): extents_{{rows, cols}}, stride_{(cols + cache_line_doubles - 1) & ~(cache_line_doubles - 1)}, cells_(rows * stride_, 0.0) {}

  const std::array<std::size_t, 2>& extents() const { 
    return extents_; 
  }

  std::size_t rows() const { 
    return extents_[0]; 
  }

  std::size_t cols() const { 
    return extents_[1]; 
  }

  std::size_t stride() const { 
    return stride_; 
  }

  double& operator()(std::size_t i, std::size_t j) { return cells_[i * stride_ + j]; }
  double operator()(std::size_t i, std::size_t j) const { return cells_[i * stride_ + j]; }

  const double* data() const { return cells_.data(); }
  double* data() { return cells_.data(); }
};  

/* views: 
- Grid owns memory
- views hold pointer to that same memory, but it doesn't own it
- view going out of scope does not free the memory
- copying a Grid copies the field
- copying a view copies the pointer plus extents and stride 
- rationale behind extents: rows and cols are always used together */
template <typename T>
struct GridView {
  T* cells;
  std::array<std::size_t, 2> extents;
  std::size_t stride;
  std::size_t rows() const { return extents[0]; }
  std::size_t cols() const { return extents[1]; }
  T& operator()(std::size_t i, std::size_t j) const { return cells[i * stride + j]; }
};

inline double stencil(const double* top, const double* center, const double* bottom, std::size_t col) {
  return 0.5 * center[col] + 0.125 * (top[col] + bottom[col] + center[col - 1] + center[col + 1]);
}

inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  GridView<const double> in{old_grid.data(), old_grid.extents(), old_grid.stride()};
  GridView<double> out{new_grid.data(), new_grid.extents(), new_grid.stride()};

  // hand copying of top and bottom boundary rows to copy_n
  // copy_n uses wide instructions: good in this case where memory is contiguous
  // includes the four corners, so the side copies below skip the first and last rows
  std::copy_n(in.cells, in.cols(), out.cells);
  std::copy_n(in.cells + (in.rows() - 1) * in.stride, in.cols(), out.cells + (out.rows() - 1) * out.stride);

  /* my processor has 6 performance cores and 8 efficiency cores. efficiency cores are slower, so we want less work on those.
  #pragma omp parallel for splits rows into equal chunks by default. dynamic schedule hands out work as threads become free, 
  so fast cores take more chunks. hinders performance on evaluator though, so i stuck with static
  
  observation: cores' private caches are too small to hold any meaningful fraction of a whole grid, so they need to fetch from the shared cache
  at the start of each time step. a lot of data is moved on the shared interconnect, so there's a cache bandwidth bottleneck s.t. 
  increasing # of threads only worsens performance. on my machine, it flattened after 6 threads.*/ 
  #pragma omp parallel for schedule(static)
  for (std::size_t i = 1; i < in.rows() - 1; i++) {
    // none of the pointers are aliased, so restrict is appropriate
    const double* __restrict top = in.cells + (i - 1) * in.stride;
    const double* __restrict center = in.cells + i * in.stride;
    const double* __restrict bottom = in.cells + (i + 1) * in.stride;
    double* __restrict out_row = out.cells + i * out.stride;

    const std::size_t last_col = in.cols() - 1;
    out_row[0] = center[0];
    out_row[last_col] = center[last_col];

    // peel 1,2,3 so the wide loop starts at a multiple of 4 (32 byte aligned if the row is)
    std::size_t j = 1;
    for (; j < last_col && (j % simd_doubles) != 0; j++) {
      out_row[j] = stencil(top, center, bottom, j);
    }

    // rows are consecutive in memory and independent. can fetch and add consecutive top, bottom, and center rows
    // now we start at a multiple of 32 bytes
    #pragma omp simd
    for (std::size_t k = j; k < last_col; k++) {
      out_row[k] = stencil(top, center, bottom, k);
    }
    // leftover interior columns (i.e. not divisible by 4)are peeled one at a time
  }
}
