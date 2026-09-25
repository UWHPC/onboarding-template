#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <vector>

namespace detail {

// 64-byte aligned so every row starts on a cache line.
template <typename T, std::size_t Alignment>
struct aligned_allocator {
  static_assert((Alignment & (Alignment - 1)) == 0, "Alignment must be a power of two");
  static_assert(Alignment >= alignof(T), "Alignment must be at least alignof(T)");

  using value_type = T;

  template <typename U>
  struct rebind { using other = aligned_allocator<U, Alignment>; };

  aligned_allocator() noexcept = default;

  template <typename U>
  aligned_allocator(const aligned_allocator<U, Alignment>&) noexcept { }

  T* allocate(std::size_t n) {
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_alloc();
    }
    return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{Alignment}));
  }

  void deallocate(T* p, std::size_t) noexcept {
    ::operator delete(p, std::align_val_t{Alignment});
  }
};

template <typename T, typename U, std::size_t Alignment>
bool operator==(const aligned_allocator<T, Alignment>&, const aligned_allocator<U, Alignment>&) noexcept {
  return true;
}

template <typename T, typename U, std::size_t Alignment>
bool operator!=(const aligned_allocator<T, Alignment>&, const aligned_allocator<U, Alignment>&) noexcept {
  return false;
}

}  // namespace detail

// row(i) is the only way to a row base; nothing outside Grid indexes by cols.
// T = double is the writable view, T = const double the read-only one. The
// converting constructor only exists when U* converts to T*, so a mutable view
// becomes a const one and never the reverse.
template <typename T>
struct BasicGridView {
  T* data;
  std::size_t rows;
  std::size_t cols;
  std::size_t stride;

  BasicGridView(T* data_, std::size_t rows_, std::size_t cols_, std::size_t stride_)
    : data{data_}, rows{rows_}, cols{cols_}, stride{stride_} { }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
  BasicGridView(const BasicGridView<U>& other)
    : data{other.data}, rows{other.rows}, cols{other.cols}, stride{other.stride} { }

  T* row(std::size_t i) const { return data + i * stride; }
};

using GridView = BasicGridView<double>;
using ConstGridView = BasicGridView<const double>;

class Grid {
private:
  static constexpr std::size_t kAlignmentBytes = 64;
  static constexpr std::size_t kAlignmentDoubles = kAlignmentBytes / sizeof(double);

  // cols rounded up to a cache line; the padding is storage only.
  static std::size_t padded_stride(std::size_t cols) {
    return (cols + kAlignmentDoubles - 1) / kAlignmentDoubles * kAlignmentDoubles;
  }

  std::size_t rows_;
  std::size_t cols_;
  std::size_t stride_;
  std::vector<double, detail::aligned_allocator<double, kAlignmentBytes>> data_;

public:
  Grid(std::size_t rows, std::size_t cols)
    : rows_{rows}, cols_{cols}, stride_{padded_stride(cols)}, data_(rows * stride_, 0.0) { }

  std::size_t rows() const { return rows_; }
  std::size_t cols() const { return cols_; }
  std::size_t stride() const { return stride_; }

  GridView view() { return GridView{data_.data(), rows_, cols_, stride_}; }
  ConstGridView view() const { return ConstGridView{data_.data(), rows_, cols_, stride_}; }

  double& operator()(std::size_t i, std::size_t j) { return data_[i * stride_ + j]; }
  double  operator()(std::size_t i, std::size_t j) const { return data_[i * stride_ + j]; }
};

namespace detail {

// restrict holds: out is in the other grid, the inputs are only read.
inline void stencil_row(
  const double* __restrict up,
  const double* __restrict mid,
  const double* __restrict down,
  double* __restrict out,
  std::size_t cols
) {
  out[0] = mid[0];
#pragma omp simd
  for (std::size_t j = 1; j < cols - 1; ++j) {
    out[j] = 0.5 * mid[j] +
             0.125 * (up[j] + down[j] + mid[j - 1] + mid[j + 1]);
  }
  out[cols - 1] = mid[cols - 1];
}

inline void copy_row(const double* src, double* dst, std::size_t cols) {
  std::memcpy(dst, src, cols * sizeof(double));
}

}  // namespace detail

inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  assert(&old_grid != &new_grid);
  assert(old_grid.rows() == new_grid.rows() && old_grid.cols() == new_grid.cols());

  const std::size_t rows = old_grid.rows();
  const std::size_t cols = old_grid.cols();

  if (rows == 0 || cols == 0) {
    return;
  }

  const ConstGridView in = old_grid.view();
  const GridView out = new_grid.view();

  if (rows < 3 || cols < 3) {
    for (std::size_t i = 0; i < rows; ++i) {
      detail::copy_row(in.row(i), out.row(i), cols);
    }
    return;
  }

  detail::copy_row(in.row(0), out.row(0), cols);
  detail::copy_row(in.row(rows - 1), out.row(rows - 1), cols);

  // Only the pragma reads these. Small grids skip the fork/join.
  [[maybe_unused]] constexpr std::size_t kMinInteriorCellsForParallel = std::size_t{1} << 14;
  [[maybe_unused]] const std::size_t interior_cells = (rows - 2) * (cols - 2);

  // static: equal rows, one contiguous band per thread.
#pragma omp parallel for schedule(static) if(interior_cells >= kMinInteriorCellsForParallel)
  for (std::size_t i = 1; i < rows - 1; ++i) {
    detail::stencil_row(in.row(i - 1), in.row(i), in.row(i + 1), out.row(i), cols);
  }
}
