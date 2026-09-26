#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

using namespace std;

#if defined(_MSC_VER)
#define RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define RESTRICT __restrict__
#else
#define RESTRICT
#endif

class Grid;

// use a simple bounding box to track the range in which stencilling calculations are actually necessary. this is
// expected to grow by one every time step, to keep up with the boundary of heat diffusion. this performs especially
// well with the benchmark test, which uses the kCenterBlock initial condition.
class ActiveDiffusionDomain {

public:
  struct BBox {
    size_t row_min, col_min, row_max, col_max;

    BBox() : row_min(1), col_min(1), row_max(0), col_max(0) {}
    BBox(size_t row_min, size_t col_min, size_t row_max, size_t col_max)
        : row_min(row_min), col_min(col_min), row_max(row_max), col_max(col_max) {}

    // we take advantage of the fact that Grid::fit_bbox's default empty return
    // value has min > max
    bool is_empty() { return row_min > row_max || col_min > col_max; }

    // expand all borders by 1 activate grid cells that may take on new values
    void grow(size_t rows, size_t cols) {
      row_min = row_min > 0 ? row_min - 1 : 0;
      col_min = col_min > 0 ? col_min - 1 : 0;
      row_max = row_max < rows - 1 ? row_max + 1 : rows - 1;
      col_max = col_max < cols - 1 ? col_max + 1 : cols - 1;
    }
  };

private:
  // bounding box of active heat spread domain
  BBox bbox_;

  // whether bbox_ is expected to reflect the current state of the target Grid. external grid modification operations
  // may touch cells that lie outside the bounding box. we err on the side of caution and force
  bool is_valid_ = false;

public:
  void invalidate() noexcept { is_valid_ = false; }
  bool is_valid() const noexcept { return is_valid_; }

  BBox &bbox() noexcept { return bbox_; }
  const BBox &bbox() const noexcept { return bbox_; }

  // determine a bounding box that encloses all nonzero values of the given grid data
  void fit_grid(const Grid &grid);
};

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
  friend void apply_stencil(const Grid &old_grid, Grid &new_grid);

private:
  struct GridView {
    size_t rows, cols, stride;
    double *grid;
  };

  size_t rows_;
  size_t cols_;
  // column count rounded up to multiple of 8. ensures grid_ column sections are
  // stored as multiples of 8 * sizeof(double) = 64 bytes for simd
  size_t stride_;

  // grid array
  // 64-byte aligned allocation for that juicy simd
  double *grid_;

  // ActiveDiffusionDomain instance attached to this Grid
  // if possible, i would have preferred to pass this as another parameter to apply_stencil to separate this from the
  // internal Grid logic, but we are given a fixed signature so this is the next best thing i can do
  ActiveDiffusionDomain domain_;

  // private access to grid view with mutable data pointer
  GridView mutable_view() noexcept { return GridView{rows_, cols_, stride_, grid_}; }

public:
  struct ConstGridView {
    size_t rows, cols, stride;
    const double *grid;
  };

  Grid(size_t rows, size_t cols)
      : rows_(rows), cols_(cols),
        // round up to multiple of 8 via bit mask
        stride_((cols + 7) & ~7),
        grid_(static_cast<double *>(::operator new[](rows * stride_ * sizeof(double), align_val_t{64}))) {}

  ~Grid() { ::operator delete[](grid_, align_val_t(64)); }

  // disable copy construction, copy assignment - each grid_ instance should
  // only be accessed by one Grid at any time.
  Grid(const Grid &) = delete;
  Grid &operator=(const Grid &) = delete;

  // move operations (used by std::swap that is called in main.cpp)
  Grid(Grid &&other) noexcept
      : rows_(other.rows_), cols_(other.cols_), stride_(other.stride_), grid_(other.grid_), domain_(other.domain_) {
    other.grid_ = nullptr;
    other.rows_ = 0;
    other.cols_ = 0;
    other.stride_ = 0;
    other.domain_.invalidate();
  }
  Grid &operator=(Grid &&other) noexcept {
    if (this != &other) {
      // free grid memory about to be replaced
      ::operator delete[](grid_, align_val_t{64});

      rows_ = other.rows_;
      cols_ = other.cols_;
      stride_ = other.stride_;
      grid_ = other.grid_;
      domain_ = other.domain_;

      other.grid_ = nullptr;
      other.rows_ = 0;
      other.cols_ = 0;
      other.stride_ = 0;
      other.domain_.invalidate();
    }
    return *this;
  }

  // the publicly-exposed ConstGridView contains only a read-only reference to the grid data, to enforce the
  // ActiveDiffusionDomain in operator().
  ConstGridView view() const noexcept { return ConstGridView{rows_, cols_, stride_, grid_}; }

  const ActiveDiffusionDomain &domain() const noexcept { return domain_; }
  ActiveDiffusionDomain &domain() noexcept { return domain_; }

  double &operator()(size_t i, size_t j) noexcept {
    // invalidate ActiveDiffusionDomain if there is a potential attempt to modify it
    domain_.invalidate();
    return grid_[i * stride_ + j];
  }
  double operator()(size_t i, size_t j) const noexcept { return grid_[i * stride_ + j]; }
};

void ActiveDiffusionDomain::fit_grid(const Grid &grid) {
  const Grid::ConstGridView view = grid.view();
  const size_t rows = view.rows, cols = view.cols, stride = view.stride;

  bbox_ = BBox{rows - 1, cols - 1, 0, 0};

  for (size_t i = 0; i < rows; ++i) {
    size_t col_min = 0;
    while (col_min < cols && view.grid[i * stride + col_min] == 0.0)
      ++col_min;

    if (col_min == cols) continue;

    size_t col_max = cols - 1;
    while (col_max > col_min && view.grid[i * stride + col_max] == 0.0)
      --col_max;

    bbox_.row_min = min(bbox_.row_min, i);
    bbox_.row_max = max(bbox_.row_max, i);
    bbox_.col_min = min(bbox_.col_min, col_min);
    bbox_.col_max = max(bbox_.col_max, col_max);
  }

  is_valid_ = true;
}

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid &old_grid, Grid &new_grid) {
  const Grid::ConstGridView old_view = old_grid.view();
  const Grid::GridView new_view = new_grid.mutable_view();

  const size_t rows = old_view.rows, cols = old_view.cols, stride = old_view.stride;

  const double *RESTRICT old_ptr = old_view.grid;
  double *RESTRICT new_ptr = new_view.grid;

  const ActiveDiffusionDomain &old_domain = old_grid.domain();
  ActiveDiffusionDomain &new_domain = new_grid.domain();

  if (old_domain.is_valid()) {
    new_domain = old_domain;
  } else {
    new_domain.fit_grid(old_grid);
    // initial pass: reset all values to zero because values outside the bounding box are skipped. we can count on this
    // working because we know old_grid and new_grid are swapped every timestep. this is a bit of a hack for
    memset(new_ptr, 0, rows * stride * sizeof(*new_ptr));
  }
  ActiveDiffusionDomain::BBox &bbox = new_domain.bbox();
  bbox.grow(rows, cols);

  if (bbox.is_empty()) return;

  // handle boundary conditions separately to avoid branching in loop
  for (size_t i = bbox.row_min; i <= bbox.row_max; ++i) {
    new_ptr[i * stride] = old_ptr[i * stride];
    new_ptr[i * stride + cols - 1] = old_ptr[i * stride + cols - 1];
  }

  const size_t last_row = (rows - 1) * stride, num_cols = bbox.col_max - bbox.col_min + 1;
  memcpy(new_ptr + bbox.col_min, old_ptr + bbox.col_min, num_cols * sizeof(*old_ptr));
  memcpy(new_ptr + last_row + bbox.col_min, old_ptr + last_row + bbox.col_min, num_cols * sizeof(*old_ptr));

#pragma omp parallel for schedule(static)
  for (size_t i = max(bbox.row_min, size_t{1}); i <= min(bbox.row_max, rows - 2); ++i) {
#pragma omp simd
    for (size_t j = max(bbox.col_min, size_t{1}); j <= min(bbox.col_max, cols - 2); ++j) {
      size_t idx = i * stride + j;
      new_ptr[idx] = 0.5 * old_ptr[idx] +
                     0.125 * (old_ptr[idx - stride] + old_ptr[idx + stride] + old_ptr[idx - 1] + old_ptr[idx + 1]);
    }
  }
}
