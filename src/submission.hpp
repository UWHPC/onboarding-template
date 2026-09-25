#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <new>
#include <vector>

// Every row starts on a 64-byte boundary
inline constexpr std::size_t CacheLineBytes = 64;
inline constexpr std::size_t DoublesPerCacheLine = CacheLineBytes / sizeof(double);  // 8

// std::vector only guarantees alignof(double) == 8 bytes. 
// This allocator makes sure that every row of the grid starts on a cache line boundary
template <typename T>
struct AlignedAllocator {
  using value_type = T;

  AlignedAllocator() = default;
  template <typename U>
  AlignedAllocator(const AlignedAllocator<U>&) {}

  T* allocate(std::size_t n) {
    return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{CacheLineBytes}));
  }
  void deallocate(T* p, std::size_t) {
    ::operator delete(p, std::align_val_t{CacheLineBytes});
  }

  template <typename U>
  bool operator==(const AlignedAllocator<U>&) const { return true; }
  template <typename U>
  bool operator!=(const AlignedAllocator<U>&) const { return false; }
};

// Size of a grid. Rows and cols are always together.
struct Dimensions {
  std::size_t rows = 0;
  std::size_t cols = 0;

  friend bool operator==(Dimensions a, Dimensions b) { return a.rows == b.rows && a.cols == b.cols; }
};

// Read-only is a separate type so signatures show which grid can change.
struct ConstGridView {
  const double* data = nullptr;
  Dimensions dims;
  std::size_t stride = 0;

  const double* row(std::size_t i) const { return data + i * stride; }
  double operator()(std::size_t i, std::size_t j) const { return data[i * stride + j]; }
};

struct GridView {
  double* data = nullptr;
  Dimensions dims;
  std::size_t stride = 0;

  double* row(std::size_t i) const { return data + i * stride; }
  double& operator()(std::size_t i, std::size_t j) const { return data[i * stride + j]; }

  operator ConstGridView() const { return {data, dims, stride}; }
};

// The end of the view, one past the last element in the last row
inline const double* view_end(ConstGridView v) {
  if (v.dims.rows == 0 || v.dims.cols == 0) return v.data;
  return v.data + (v.dims.rows - 1) * v.stride + v.dims.cols;
}

// Backs up the __restrict promise. std::less, since < on unrelated pointers is unspecified.
inline bool overlaps(ConstGridView a, ConstGridView b) {
  const std::less<const double*> before;
  return before(a.data, view_end(b)) && before(b.data, view_end(a));
}

// One aligned buffer, element (i, j) at i * stride + j. stride is cols rounded
// up to a multiple of 8 doubles, so every row starts aligned.
class Grid {
private:
  Dimensions dims_;
  std::size_t stride_;
  std::vector<double, AlignedAllocator<double>> data_;

  static std::size_t round_up(std::size_t n) {
    return (n + DoublesPerCacheLine - 1) / DoublesPerCacheLine * DoublesPerCacheLine;
  }

public:
  Grid(std::size_t rows, std::size_t cols)
      : dims_{rows, cols}, stride_(round_up(cols)), data_(rows * stride_, 0.0) {}

  double& operator()(std::size_t i, std::size_t j) {
    assert(i < dims_.rows && j < dims_.cols && "grid index out of range");
    return data_[i * stride_ + j];
  }
  double operator()(std::size_t i, std::size_t j) const {
    assert(i < dims_.rows && j < dims_.cols && "grid index out of range");
    return data_[i * stride_ + j];
  }

  // The kernel only sees views, never ownership. A const Grid gives a read-only view.
  GridView      view()       { return {data_.data(), dims_, stride_}; }
  ConstGridView view() const { return {data_.data(), dims_, stride_}; }
};

// Below this, starting threads costs more than the work saves. A guess, not tuned:
// our test laptop was too noisy to measure where the crossover really is.
inline constexpr std::size_t ParallelMinCells = 1 << 16;

// Computes one interior row. __restrict tells the compiler that out does not
// overlap up, mid or down, so it can vectorize without runtime overlap checks.
inline void stencil_row(const double* __restrict up, const double* __restrict mid,
                        const double* __restrict down, double* __restrict out, std::size_t cols) {
  const std::size_t end = cols == 0 ? 0 : cols - 1;
  #ifdef _OPENMP
    #pragma omp simd
  #endif
  for (std::size_t j = 1; j < end; ++j) {
    out[j] = 0.5 * mid[j] + 0.125 * (up[j] + down[j] + mid[j - 1] + mid[j + 1]);
  }
}

inline void stencil(ConstGridView old_grid, GridView new_grid) {
  const std::size_t rows = old_grid.dims.rows;
  const std::size_t cols = old_grid.dims.cols;

  // Assertions to check that the grids are compatible
  assert(new_grid.dims == old_grid.dims && "old and new grids must be the same size");
  assert(old_grid.stride >= cols && new_grid.stride >= cols && "a row can't be longer than its stride");
  assert(!overlaps(old_grid, new_grid) && "old and new must not share memory");
  if (rows == 0 || cols == 0) return;

  // Top and bottom boundary rows are copied unchanged
  for (std::size_t j = 0; j < cols; ++j) {
    new_grid(0, j) = old_grid(0, j);
    new_grid(rows - 1, j) = old_grid(rows - 1, j);
  }

  // Rows are independent, so threads can each take a block of rows.
  // Each thread also copies the left/right boundary cells of its own rows
  // Small grids stay on one thread
  const std::size_t last = rows - 1;
  #ifdef _OPENMP
    #pragma omp parallel for schedule(static) if (rows * cols >= ParallelMinCells)
  #endif
  for (std::size_t i = 1; i < last; ++i) {
    new_grid(i, 0) = old_grid(i, 0);
    stencil_row(old_grid.row(i - 1), old_grid.row(i), old_grid.row(i + 1), new_grid.row(i), cols);
    new_grid(i, cols - 1) = old_grid(i, cols - 1);
  }
}

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid.
inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  stencil(old_grid.view(), new_grid.view());
}
