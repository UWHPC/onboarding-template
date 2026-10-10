#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

#ifdef __AVX__
#include <immintrin.h>
#endif

// Aligns storage to 64 bytes and pads the stride so every row starts aligned.
template <typename T> class PaddedAlignedVector {
private:
  // Target the 64-byte cache lines on x86 CPUs.
  static constexpr std::size_t CACHE_ALIGNMENT = 64;

  static_assert(CACHE_ALIGNMENT % sizeof(T) == 0);
  static constexpr std::size_t elements_per_line = CACHE_ALIGNMENT / sizeof(T);

  // Allocates for an address that is in alignment of 64 byte. Follows the Allocator format for vectors
  template <typename U> class CacheAlignAllocator {
  public:
    using value_type = U;

    CacheAlignAllocator() noexcept = default;

    template <typename V>
    CacheAlignAllocator(const CacheAlignAllocator<V>&) noexcept {}

    template <typename V>
    bool operator==(const CacheAlignAllocator<V>&) const noexcept {
      return true;
    }

    template <typename V>
    bool operator!=(const CacheAlignAllocator<V>&) const noexcept {
      return false;
    }

    U* allocate(std::size_t count) {
      if (count > std::numeric_limits<std::size_t>::max() / sizeof(U)) {
        throw std::bad_array_new_length{};
      }

      return static_cast<U*>(
          ::operator new(count * sizeof(U), std::align_val_t{CACHE_ALIGNMENT}));
    }

    void deallocate(U* pointer, std::size_t) noexcept {
      ::operator delete(pointer, std::align_val_t{CACHE_ALIGNMENT});
    }
  };

  // The *real* columns length of the vector that is essentially ceil(sizeof(T) * column / 64) * 64 
  // Paired with the 64 bytes address alignment, this padding ensures that the data retrieval will not
  // need to access data from previous or next rows for the first and last group of data along a row 
  std::size_t stride_;

  std::vector<T, CacheAlignAllocator<T>> storage_;

public:
  PaddedAlignedVector(std::size_t rows, std::size_t cols) : stride_{0} {
    constexpr std::size_t max_size{std::numeric_limits<std::size_t>::max()};
    if (cols > max_size - (elements_per_line - 1)) {
      throw std::bad_array_new_length{};
    }

    // Essentially ceil(sizeof(T) * column / 64) * 64
    stride_ = (cols + elements_per_line - 1) / elements_per_line * elements_per_line;

    if (stride_ != 0 && rows > (max_size / sizeof(T)) / stride_) {
      throw std::bad_array_new_length{};
    }

    storage_.resize(rows * stride_, T{});
  }

  inline T* ptr(std::size_t i, std::size_t j) {
    return storage_.data() + i * stride_ + j;
  }

  inline const T* ptr(std::size_t i, std::size_t j) const {
    return storage_.data() + i * stride_ + j;
  }

  inline T& operator()(std::size_t i, std::size_t j) { return *ptr(i, j); }

  inline const T& operator()(std::size_t i, std::size_t j) const {
    return *ptr(i, j);
  }
};

// This is a sample structure created to keep track of the active area (non-zero section) expanding
// from the heatmap within a rectangular area. It is rectangular because it is cheap to calculate 
// its expansion rather than some complex structure to describe any potential complex geometry. 
struct ActiveArea {
  std::size_t first_row{0}, end_row{0};
  std::size_t first_col{0}, end_col{0};

  bool empty() const { return first_row == end_row || first_col == end_col; }

  void include(std::size_t row, std::size_t col) {
    if (empty()) {
      *this = {row, row + 1, col, col + 1};
    } else {
      first_row = std::min(first_row, row);
      end_row = std::max(end_row, row + 1);
      first_col = std::min(first_col, col);
      end_col = std::max(end_col, col + 1);
    }
  }

  // By the formula, it is only poossible that the active area grows by one grid around the 
  // existing active area. Thus, this expands the active area boundary by 1 on all 4 sides if
  // not at the grid boundary. 
  void expand(std::size_t rows, std::size_t cols) {
    if (empty()) {
      return;
    }
    if (first_row > 0) --first_row;
    if (first_col > 0) --first_col;
    if (end_row < rows) ++end_row;
    if (end_col < cols) ++end_col;
  }
};

class GridView;

//Class for containing the heatmap data 
class Grid {
private:
  // Grid information 
  std::size_t rows_;
  std::size_t cols_;
  PaddedAlignedVector<double> grid_;

  // We have to make this mutable because we need to scan an intiail grid under a const
  // reference. We keep it as a grid state since std::swap is done in the benchmark
  // after each iteration, meaning keeping it lets us avoid the costly recacluation of finding
  // the active area
  mutable ActiveArea active_{};
  // We assume this as false because by default the grid starts will all zero
  mutable bool active_dirty_{false};

  // Finds the active area on a grid only if the active is dirty, meaning the grid data has 
  // been manipulated.
  void detect_active_area() const {
    if (!active_dirty_) return;
    active_ = {};
    for (std::size_t row = 0; row < rows_; ++row) {
      for (std::size_t col = 0; col < cols_; ++col) {
        if (grid_(row, col) != 0.0) active_.include(row, col);
      }
    }
    active_dirty_ = false;
  }

  // To reset a grid back to all zero, we only infer upon clearing its active area outside of the other grid's 
  // active area that our stencil is reading upon. Since active area cannot decrease, it is not necessary to clear
  // data within the other's active area as they would be overwritten by our stencil. 
  void clear_stale_values(const ActiveArea& next) {
    detect_active_area();
    const ActiveArea& previous{active_};

    if (previous.empty() ||
        (!next.empty() && next.first_row <= previous.first_row &&
         next.end_row >= previous.end_row && next.first_col <= previous.first_col &&
         next.end_col >= previous.end_col)) {
      return;
    }

    for (std::size_t row = previous.first_row; row < previous.end_row; ++row) {
      for (std::size_t col = previous.first_col; col < previous.end_col; ++col) {
        if (row < next.first_row || row >= next.end_row ||
            col < next.first_col || col >= next.end_col) {
          grid_(row, col) = 0.0;
        }
      }
    }
  }

  // Friend because they need access to the 2 private methods that is only for our stencil calculation
  friend class GridView;
  friend void apply_stencil_impl(const GridView& source, Grid& destination);

public:

  // This proxy is for determining whether a mutable reference sent out is really being used
  // for mutation to determine whether the active area is dirty. It's not super 
  // necessary since you could set it as dirty if the ref is sent like many others have 
  // done, but resetting the grid is potentially expensive since it requires additional 
  // memory access 

  class CellProxy {
    Grid& owner_;
    std::size_t row_, col_;

  public:
    CellProxy(Grid& owner, std::size_t row, std::size_t col)
        : owner_{owner}, row_{row}, col_{col} {}
    operator double() const { return owner_.grid_(row_, col_); }
    CellProxy& operator=(double value) {
      owner_.grid_(row_, col_) = value;
      owner_.active_dirty_ = true;
      return *this;
    }
    CellProxy& operator=(const CellProxy& other) {
      return *this = static_cast<double>(other);
    }
  };

  Grid(std::size_t rows, std::size_t cols)
      : rows_{rows}, cols_{cols}, grid_{rows, cols} {}

  const std::size_t& rows() const {return rows_;}
  const std::size_t& cols() const {return cols_;}

  CellProxy operator()(std::size_t row, std::size_t col) {
    return {*this, row, col};
  }
  double operator()(std::size_t row, std::size_t col) const {
    return grid_(row, col);
  }
};

// Read-only access to an existing grid. Frankly, I only did this because of the criteria. Little 
// confused as to why since const ref exist. Perhaps a Rust reference? 
class GridView {
  const Grid& grid_;
  ActiveArea& active_;

  // made it friend so it can access the field active_
  friend void apply_stencil_impl(const GridView& source, Grid& destination);

public:
  explicit GridView(const Grid& grid) : grid_{grid}, active_(grid.active_) {
    grid.detect_active_area();
  }

  std::size_t rows() const { return grid_.rows(); }
  std::size_t cols() const { return grid_.cols(); }

  inline const double* ptr(std::size_t row, std::size_t col) const {
    return grid_.grid_.ptr(row, col);
  }
  inline double operator()(std::size_t row, std::size_t col) const {
    return grid_(row, col);
  }
};

// Updates along a row of the stencil code. Explicitly uses AVX (4 row update at a time) and leave it
// to pragma simd when not possible. 
//
// A Note: 
//
// SIMD doesn't actually affect performance much in this stencil kernel from testing with or without. 
// It's probably because reading or storing into memory takes 100s of clock cycles while the actual operation 
// only takes maybe a few, so making your operations faster probably doesn't matter when 99% of the time is in
// memory read/write. 
inline void update_row(const double* top, const double* mid,
                       const double* bottom, double* out,
                       std::size_t col, std::size_t end_col) {
#ifdef __AVX__
  const __m256d center_weight{_mm256_set1_pd(0.5)};
  const __m256d neighbor_weight{_mm256_set1_pd(0.125)};

  for (; col + 4 <= end_col; col += 4) {
    const __m256d center{_mm256_loadu_pd(mid + col)};
    __m256d neighbors = _mm256_add_pd(_mm256_loadu_pd(top + col), _mm256_loadu_pd(bottom + col));
    neighbors = _mm256_add_pd(neighbors, _mm256_loadu_pd(mid + col - 1));
    neighbors = _mm256_add_pd(neighbors, _mm256_loadu_pd(mid + col + 1));

    const __m256d result =
        _mm256_add_pd(_mm256_mul_pd(center_weight, center),
                      _mm256_mul_pd(neighbor_weight, neighbors));
    _mm256_storeu_pd(out + col, result);
  }
#endif

  // Either calculate what remains or calculate the entire row if AVX not enabled 
  #pragma omp simd
  for (std::size_t i = col; i < end_col; ++i) {
    out[i] = 0.5 * mid[i] + 0.125 * (top[i] + bottom[i] + mid[i - 1] + mid[i + 1]);
  }
}

// Actual stencil kernel implementation
inline void apply_stencil_impl(const GridView& input, Grid& destination) {
  const std::size_t rows{input.rows()}, cols{input.cols()};

  // Update the new destination grid's active area and clears its old active area.
  ActiveArea next{input.active_};
  next.expand(rows, cols);
  destination.clear_stale_values(next);
  destination.active_ = next;
  destination.active_dirty_ = false;
  
  if (next.empty()) {
    return;
  }

  // This is for the later update row, but we put it here since this gets re-used
  // We clamp it because update_row should not operate on column boundaries 
  const std::size_t first_col{std::max(next.first_col, std::size_t{1})};
  const std::size_t end_col{std::min(next.end_col, cols - 1)};
  const std::size_t width{next.end_col - next.first_col};

  #pragma omp parallel for schedule(static)
  for (std::size_t row = next.first_row; row < next.end_row; ++row) {
    const double* mid{input.ptr(row, 0)};
    double* __restrict out{destination.grid_.ptr(row, 0)};

    // The outer edges keep their original temperatures
    if (row == 0 || row + 1 == rows) {
      std::memcpy(out + next.first_col, mid + next.first_col,width * sizeof(double));
      continue;
    }

    if (next.first_col == 0) {
      out[0] = mid[0];
    }
    if (next.end_col == cols) {
      out[cols - 1] = mid[cols - 1];
    }

    update_row(
      input.ptr(row - 1, 0), 
      mid,
      input.ptr(row + 1, 0), 
      out, 
      first_col, 
      end_col
    );
  }
}

inline void apply_stencil(const Grid& source, Grid& destination) {
  //Sanity checks.
  if (source.rows() != destination.rows() || source.cols() != destination.cols()) {
    throw std::invalid_argument("Grid dimensions must match");
  }
  if (&source == &destination) {
    throw std::invalid_argument("Stencil needs distinct source and destination grids");
  }
  apply_stencil_impl(GridView{source}, destination);
}
