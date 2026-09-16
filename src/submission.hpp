#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <new>
#include <vector>

inline constexpr std::size_t OPENMP_MIN_CELLS = 4096;
inline constexpr std::size_t CACHE_LINE_SIZE = 64;
inline constexpr std::size_t CACHE_LINE_ELEMENTS =
    CACHE_LINE_SIZE / sizeof(double);
inline constexpr std::size_t ROW_PREFIX_ELEMENTS = CACHE_LINE_ELEMENTS - 1;

// Allocator that gives vector storage a cacheline-aligned
// base address.
template <typename T, std::size_t Alignment> class AlignedAllocator {
public:
  using value_type = T;

  static_assert(Alignment >= alignof(T));
  static_assert((Alignment & (Alignment - 1)) == 0);

  AlignedAllocator() noexcept = default;

  template <typename U>
  AlignedAllocator(const AlignedAllocator<U, Alignment> &) noexcept {}

  [[nodiscard]] T *allocate(std::size_t count) {
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
      throw std::bad_array_new_length{};

    return static_cast<T *>(
        ::operator new(count * sizeof(T), std::align_val_t{Alignment}));
  }

  void deallocate(T *pointer, std::size_t) noexcept {
    ::operator delete(pointer, std::align_val_t{Alignment});
  }

  template <typename U> struct rebind {
    using other = AlignedAllocator<U, Alignment>;
  };
};

template <typename T, typename U, std::size_t Alignment>
bool operator==(const AlignedAllocator<T, Alignment> &,
                const AlignedAllocator<U, Alignment> &) noexcept {
  return true;
}

template <typename T, typename U, std::size_t Alignment>
bool operator!=(const AlignedAllocator<T, Alignment> &,
                const AlignedAllocator<U, Alignment> &) noexcept {
  return false;
}

template <std::size_t Rank> struct Extents {
  std::array<std::size_t, Rank> dimensions;

  template <std::size_t Dimension>
  constexpr std::size_t extent() const noexcept {
    static_assert(Dimension < Rank);
    return dimensions[Dimension];
  }
};

// Non-owning view with a compile-time alignment contract for its data handle.
template <typename T, std::size_t Rank,
          std::size_t Alignment = alignof(T),
          std::size_t AlignmentOffset = 0>
class View {
  static_assert(Alignment >= alignof(T));
  static_assert((Alignment & (Alignment - 1)) == 0);
  static_assert(AlignmentOffset < Alignment);
  static_assert(AlignmentOffset % alignof(T) == 0);

  T *data_;
  Extents<Rank> extents_;

public:
  constexpr View(T *data, Extents<Rank> extents) noexcept
      : data_(data), extents_(extents) {}

  T *data() const noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<T *>(__builtin_assume_aligned(
        data_, Alignment, AlignmentOffset));
#else
    return data_;
#endif
  }

  template <std::size_t Dimension>
  constexpr std::size_t extent() const noexcept {
    return extents_.template extent<Dimension>();
  }

  T &operator[](std::size_t index) const noexcept { return data()[index]; }
  constexpr std::size_t size() const noexcept {
    static_assert(Rank == 1);
    return extent<0>();
  }
  T *begin() const noexcept { return data(); }
  T *end() const noexcept { return size() == 0 ? data() : data() + size(); }
};

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
  using storage_type =
      std::vector<double, AlignedAllocator<double, CACHE_LINE_SIZE>>;

  Extents<2> extents_;
  std::size_t stride_;
  storage_type data;

public:
  using row_view = View<double, 1, CACHE_LINE_SIZE,
                        ROW_PREFIX_ELEMENTS * sizeof(double)>;
  using const_row_view = View<const double, 1, CACHE_LINE_SIZE,
                              ROW_PREFIX_ELEMENTS * sizeof(double)>;

  // Initializes a zero-filled grid with the specified dimensions.
  Grid(std::size_t rows, std::size_t cols)
      : extents_{{rows, cols}},
        stride_((cols + ROW_PREFIX_ELEMENTS + CACHE_LINE_ELEMENTS - 1) /
                CACHE_LINE_ELEMENTS * CACHE_LINE_ELEMENTS),
        data(rows * stride_) {}

  double &operator()(std::size_t i, std::size_t j) {
    return data[ROW_PREFIX_ELEMENTS + i * stride_ + j];
  }

  double operator()(std::size_t i, std::size_t j) const {
    return data[ROW_PREFIX_ELEMENTS + i * stride_ + j];
  }

  std::size_t get_rows() const noexcept { return extents_.extent<0>(); }
  std::size_t get_cols() const noexcept { return extents_.extent<1>(); }

  row_view row(std::size_t i) noexcept {
    return {data.data() + ROW_PREFIX_ELEMENTS + i * stride_,
            Extents<1>{{extents_.extent<1>()}}};
  }

  const_row_view row(std::size_t i) const noexcept {
    return {data.data() + ROW_PREFIX_ELEMENTS + i * stride_,
            Extents<1>{{extents_.extent<1>()}}};
  }
};

namespace {

// Apply one logical row, keeping the first interior cell aligned for SIMD.
inline void apply_stencil_row(Grid::const_row_view above,
                              Grid::const_row_view center,
                              Grid::const_row_view below,
                              Grid::row_view output) noexcept {
  const std::size_t interior_cols = output.size() - 2;
  output[0] = center[0];
  output[output.size() - 1] = center[output.size() - 1];

  // Using SIMD between cells due to independence, apply the stencil kernel to
  // the interior of the row.
#pragma omp simd
  for (std::size_t j = 0; j < interior_cols; ++j) {
    output[j + 1] =
        0.125 * (above[j + 1] + center[j] + center[j + 2] + below[j + 1]) +
        0.5 * center[j + 1];
  }
}

inline void apply_stencil_interior(const Grid &old_grid, Grid &new_grid) {
  const std::size_t rows = old_grid.get_rows();
  const std::size_t cols = old_grid.get_cols();

  // Process each row in parallel, giving every thread exclusive cache-local
  // output.
#pragma omp parallel for schedule(static) if (rows * cols >= OPENMP_MIN_CELLS)
  for (std::size_t i = 1; i < rows - 1; ++i) {
    apply_stencil_row(old_grid.row(i - 1), old_grid.row(i), old_grid.row(i + 1),
                      new_grid.row(i));
  }
}

// Copy boundary rows and handle grids with no interior cells. Returns true when
// the boundary covers the entire grid and no stencil rows remain to process.
inline bool apply_stencil_boundary(const Grid &old_grid, Grid &new_grid) {
  const std::size_t rows = old_grid.get_rows();
  const std::size_t cols = old_grid.get_cols();

  if (rows == 0 || cols == 0)
    return true;

  const auto first_row = old_grid.row(0);
  std::copy(first_row.begin(), first_row.end(), new_grid.row(0).begin());

  if (rows > 1) {
    const auto last_row = old_grid.row(rows - 1);
    std::copy(last_row.begin(), last_row.end(), new_grid.row(rows - 1).begin());
  }

  if (rows < 3)
    return true;

  if (cols < 3) {
    for (std::size_t i = 1; i < rows - 1; ++i) {
      const auto old_row = old_grid.row(i);
      const auto new_row = new_grid.row(i);
      new_row[0] = old_row[0];
      if (cols == 2)
        new_row[1] = old_row[1];
    }
    return true;
  }

  return false;
}

} // namespace

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
inline void apply_stencil(const Grid &old_grid, Grid &new_grid) {
  if (apply_stencil_boundary(old_grid, new_grid))
    return;

  apply_stencil_interior(old_grid, new_grid);
}
