#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

// Role: internal helpers backing Grid and apply_stencil.
// Reason: named + inline so the external-linkage
// callers below stay well-defined if this header is included 
// from more than one translation unit.
namespace detail {

// Role: alignment target for the grid buffer, in bytes and in doubles.
// Reason: 64 covers both a cache line and the evaluator's AVX2 (32-byte)
// vector width in one number, so there's only one constant to reason about.
inline constexpr std::size_t kAlignment{64};
inline constexpr std::size_t kAlignmentDoubles{kAlignment / sizeof(double)};

// Role: leading padding before logical column 0.
// Reason: shifts column 1, the loop's real first access, onto the
// alignment boundary instead of column 0.
inline constexpr std::size_t kLeadingPadDoubles{kAlignmentDoubles - 1};

// Role: rounds a value up to the next multiple.
// Reason: generic on purpose — used below to pad stride to a whole alignment
// unit, kept separate from that use so it stays a plain, reusable helper.
constexpr std::size_t round_up_to_multiple(std::size_t value, std::size_t multiple) {
  return (value + multiple - 1) / multiple * multiple;
}

// Role: deleter and owning-pointer type for the grid's aligned buffer.
// Reason: the unsized aligned delete pairs safely with the sized aligned new
// used below as long as the alignment matches, so the deleter needs no
// stored size and a plain unique_ptr can own the buffer directly.
struct AlignedDeleter {
  void operator()(double* p) const noexcept {
    ::operator delete(p, std::align_val_t{kAlignment});
  }
};
using AlignedBuffer = std::unique_ptr<double[], AlignedDeleter>;

// Role: allocates rows*stride doubles, 64-byte aligned and zero-initialized.
// Reason: checks the multiplication first — an absurd rows/stride would
// otherwise silently wrap into a too-small allocation that a caller then
// writes past, one of the API preconditions an owning type has to enforce.
inline AlignedBuffer allocate_zeroed(std::size_t rows, std::size_t stride) {
  constexpr std::size_t max_size{std::numeric_limits<std::size_t>::max()};
  if (stride != 0 && rows > max_size / stride) {
    throw std::length_error("Grid: rows * stride overflows size_t");
  }
  const std::size_t count{rows * stride};
  if (count != 0 && count > max_size / sizeof(double)) {
    throw std::length_error("Grid: element count * sizeof(double) overflows size_t");
  }

  double* p{static_cast<double*>(
    ::operator new(count * sizeof(double), std::align_val_t{kAlignment})
  )};
  // Role: zero-fill after allocating.
  // Reason: aligned `operator new` doesn't zero memory, and Grid's contract
  // requires every cell to start at zero.
  std::fill(p, p + count, 0.0);
  return AlignedBuffer(p);
}

// Role: bundles rows+cols (Extent) and the physical row stride (Layout)
// into one value instead of three loose integers.
// Reason: rows/cols never travel apart, and neither do the grid and its
// stride; keeping them one type means extending to another dimension only
// touches Extent/Layout and the kernels' loop nest, not Grid's ownership
// or allocation code.
struct Extent {
  std::size_t rows;
  std::size_t cols;
};

struct Layout {
  Extent extent;
  // Physical distance between row starts: kLeadingPadDoubles + extent.cols,
  // rounded up so every row also starts aligned.
  std::size_t stride;
};

// Role: non-owning pointer + layout into a Grid's buffer; T = double or
// const double gives the mutable/read-only forms from one definition.
// Reason: kernels need only pointer+shape, not Grid itself — keeping this
// to a handful of trivially-copyable fields means building one costs
// nothing, so apply_stencil can construct a fresh view on every call.
template <typename T>
struct BasicGridView {
  T* data;
  Layout layout;

  std::size_t rows() const { return layout.extent.rows; }
  std::size_t cols() const { return layout.extent.cols; }
  std::size_t stride() const { return layout.stride; }
};

using GridView = BasicGridView<double>;
using ConstGridView = BasicGridView<const double>;

// Role: copies the top/bottom rows only; left/right columns for interior
// rows are handled in update_row instead.
// Reason: a left/right loop here would stride a full row per touch,
// wasting a cache-line fetch on 8 bytes; update_row already has those
// addresses hot from its own neighbor reads.
inline void copy_boundary(ConstGridView old_view, GridView new_view) {
  const std::size_t rows{old_view.rows()};
  const std::size_t cols{old_view.cols()};
  const std::size_t old_stride{old_view.stride()};
  const std::size_t new_stride{new_view.stride()};

  for (std::size_t j{0}; j < cols; ++j) {
    new_view.data[kLeadingPadDoubles + j] = old_view.data[kLeadingPadDoubles + j];
    new_view.data[(rows - 1) * new_stride + kLeadingPadDoubles + j] =
      old_view.data[(rows - 1) * old_stride + kLeadingPadDoubles + j];
  }
}

// Role: five-point update for one interior row, plus that row's own
// left/right boundary cells (see copy_boundary).
// Reason: no restrict qualifier -- not standard C++, and #pragma omp simd
// below already asserts the no-aliasing it would have provided.
inline void update_row(
  std::size_t i,
  const double* old_base, double* new_base,
  std::size_t cols, std::size_t old_stride, std::size_t new_stride
) {
  // cols < 2: one column, simultaneously the left and right boundary --
  // no column 1 for the aligned path below to point at.
  if (cols < 2) {
    if (cols == 1) {
      new_base[i * new_stride + kLeadingPadDoubles] = old_base[i * old_stride + kLeadingPadDoubles];
    }
    return;
  }

  // center_row/new_row point at column 1, kAlignmentDoubles into the row
  // (past the leading pad) -- aligned, same as the row's own start.
  // up_row/down_row are a whole `stride` away, so they land on it too.
  const double* center_row = old_base + i * old_stride + kAlignmentDoubles;
  const double* up_row = center_row - old_stride;
  const double* down_row = center_row + old_stride;
  double* new_row = new_base + i * new_stride + kAlignmentDoubles;

  // Left boundary (column 0) is center_row[-1]; primes this cache line
  // before the loop's k=0 reads it as its own left neighbor.
  new_row[-1] = center_row[-1];

  // aligned(...) asserts these four pointers are kAlignment-byte aligned;
  // if it's false, then it would result in undefined behavior.
  #pragma omp simd aligned(center_row, up_row, down_row, new_row : kAlignment)
  for (std::size_t k = 0; k < cols - 2; ++k) {
    new_row[k] = 0.5   * center_row[k] +
                 0.125 * (up_row[k] + down_row[k] + center_row[k - 1] + center_row[k + 1]);
  }

  // Right boundary (column cols-1) is center_row[cols-2]; reuses the
  // cache line the loop's last iteration just read as its right neighbor.
  new_row[cols - 2] = center_row[cols - 2];
}

// Role: parallel orchestration — divides interior rows across threads.
// Reason: kept separate from update_row's arithmetic so the scheduling
// decision can change without touching the math.
inline void update_interior(ConstGridView old_view, GridView new_view) {
  const std::size_t rows{old_view.rows()};
  const std::size_t cols{old_view.cols()};
  const std::size_t old_stride{old_view.stride()};
  const std::size_t new_stride{new_view.stride()};

  // Role: plain pointers here, not restricted.
  // Reason: this function only forwards them to update_row and does no
  // arithmetic itself — the restrict promise belongs at update_row's
  // parameters, the actual kernel boundary.
  const double* old_base{old_view.data};
  double* new_base{new_view.data};

  // Role: schedule(static).
  // Reason: every row costs the same fixed amount of work, so there's no
  // imbalance for dynamic/guided to correct.
  #pragma omp parallel for schedule(static) default(none) \
      shared(rows, cols, old_stride, new_stride, old_base, new_base)
  for (std::size_t i = 1; i < rows - 1; ++i) {
    update_row(i, old_base, new_base, cols, old_stride, new_stride);
  }
}

}  // namespace detail

// Role: Grid owns one flat, aligned, zero-initialized buffer for the field.
// Reason: layout_.stride pads for both leading alignment and row rounding;
// all of it is internal-only and never surfaces through operator().
class Grid {
private:
  detail::Layout layout_;
  detail::AlignedBuffer data_;

public:
  Grid(std::size_t rows, std::size_t cols)
    : layout_{{rows, cols}, detail::round_up_to_multiple(
        detail::kLeadingPadDoubles + cols, detail::kAlignmentDoubles)}
    , data_{detail::allocate_zeroed(layout_.extent.rows, layout_.stride)}
  { }

  // Role: copy deleted, move defaulted.
  // Reason (copy): an accidental multi-megabyte buffer copy should be a
  // compile error, not a silent cost.
  // Reason (move): must be spelled out explicitly, not left implicit —
  // declaring the deleted copy operations suppresses the compiler's
  // implicit move generation too, not just copy; without these two lines
  // Grid would silently be neither copyable nor movable.
  Grid(const Grid&) = delete;
  Grid& operator=(const Grid&) = delete;
  Grid(Grid&&) = default;
  Grid& operator=(Grid&&) = default;

  double& operator()(std::size_t i, std::size_t j) {
    return data_[i * layout_.stride + detail::kLeadingPadDoubles + j];
  }

  double operator()(std::size_t i, std::size_t j) const {
    return data_[i * layout_.stride + detail::kLeadingPadDoubles + j];
  }

  // Role: non-owning access for kernels — pointer plus shape, nothing else.
  // Reason: copy_boundary/update_row/update_interior take only this as a
  // parameter and have no other access to Grid; this public accessor is
  // what replaces a friend declaration.
  detail::GridView view() {
    return detail::GridView{data_.get(), layout_};
  }

  detail::ConstGridView view() const {
    return detail::ConstGridView{data_.get(), layout_};
  }
};

// Role: builds a view of each grid, then dispatches to the boundary and
// interior kernels.
// Reason: no special access to Grid — view() is public, so apply_stencil
// is an ordinary caller, not a friend.
inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const detail::ConstGridView old_view{old_grid.view()};
  const detail::GridView new_view{new_grid.view()};

  // Role: return early on an empty grid.
  // Reason: also avoids unsigned underflow in `rows - 1` below.
  if (old_view.rows() == 0 || old_view.cols() == 0) {
    return;
  }

  detail::copy_boundary(old_view, new_view);
  detail::update_interior(old_view, new_view);
}
