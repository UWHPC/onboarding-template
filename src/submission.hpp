#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <new>
#include <vector>

namespace detail {

constexpr std::size_t kCacheLineBytes{64};

template <class T>
struct CacheAlignedAllocator {
  using value_type = T;

  CacheAlignedAllocator() = default;
  template <class U>
  CacheAlignedAllocator(const CacheAlignedAllocator<U>&) noexcept { }

  T* allocate(std::size_t n) {
    if (n > static_cast<std::size_t>(-1) / sizeof(T)) {
      throw std::bad_array_new_length{};
    }
    return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{kCacheLineBytes}));
  }

  void deallocate(T* p, std::size_t) noexcept {
    ::operator delete(p, std::align_val_t{kCacheLineBytes});
  }
};

template <class T, class U>
bool operator==(const CacheAlignedAllocator<T>&, const CacheAlignedAllocator<U>&) { return true; }
template <class T, class U>
bool operator!=(const CacheAlignedAllocator<T>&, const CacheAlignedAllocator<U>&) { return false; }

}  // namespace detail

template <class T>
struct GridView {
  T* data;
  std::size_t rows;
  std::size_t cols;
  std::size_t stride;

  T* row(std::size_t i) const { return data + i * stride; }
};

class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::size_t stride_;
  std::vector<double, detail::CacheAlignedAllocator<double>> data_;

  static std::size_t padded_stride(std::size_t cols) {
    constexpr std::size_t kDoublesPerLine{detail::kCacheLineBytes / sizeof(double)};
    return (cols + kDoublesPerLine - 1) / kDoublesPerLine * kDoublesPerLine;
  }

public:
  Grid(std::size_t rows, std::size_t cols): rows_{rows}, cols_{cols}, stride_{padded_stride(cols)}, data_(rows * stride_) { }

  std::size_t rows() const {return rows_;}
  std::size_t cols() const {return cols_;}
  std::size_t stride() const {return stride_;}

  double& operator()(std::size_t i, std::size_t j) {return data_[i * stride_ + j];}
  double operator()(std::size_t i, std::size_t j) const {return data_[i * stride_ + j];}

  GridView<double> view() {return {data_.data(), rows_, cols_, stride_};}
  GridView<const double> view() const {return {data_.data(), rows_, cols_, stride_};}
};

namespace detail {

inline void stencil_row(const double* up, const double* mid, const double* down,
                        double* out, std::size_t cols) {
  out[0] = mid[0];

  #pragma omp simd
  for (std::size_t j = 1; j < cols - 1; ++j) {
    out[j] = 0.5   * mid[j] +
             0.125 * (up[j] + down[j] + mid[j - 1] + mid[j + 1]);
  }

  out[cols - 1] = mid[cols - 1];
}

}  // namespace detail

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid.
inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  assert(old_grid.rows() == new_grid.rows() && old_grid.cols() == new_grid.cols());
  assert(&old_grid != &new_grid);

  const GridView<const double> in{old_grid.view()};
  const GridView<double> out{new_grid.view()};
  const std::size_t rows{in.rows};
  const std::size_t cols{in.cols};

  if (rows == 0 || cols == 0) {
    return;
  }

  std::copy_n(in.row(0), cols, out.row(0));
  std::copy_n(in.row(rows - 1), cols, out.row(rows - 1));

  #pragma omp parallel for schedule(static)
  for (std::size_t i = 1; i < rows - 1; ++i) {
    detail::stencil_row(in.row(i - 1), in.row(i), in.row(i + 1), out.row(i), cols);
  }
}
