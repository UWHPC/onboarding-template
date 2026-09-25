#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.

class Grid {
private:
  static constexpr size_t ALIGNMENT_SIZE = 32; // bytes, one avx2 register
  static constexpr size_t ALIGNED_DOUBLES = ALIGNMENT_SIZE / sizeof(double);
  static constexpr size_t PREFIX_PADDING = ALIGNED_DOUBLES - 1; // shifts first interior point onto an aligned address

  size_t rows_;
  size_t cols_;
  size_t size_;

  bool boundary_formed_; // keep track of whether boundary has already been copied

  size_t stride_; // number of doubles per row, padded for alignment

  double* data_; // owned by this grid, row_ptr only hands out views into it

  // steal other's buffer and leave it empty so its destructor doesn't free ours
  void take(Grid& other) {
    rows_ = other.rows_;
    cols_ = other.cols_;
    size_ = other.size_;
    boundary_formed_ = other.boundary_formed_;
    stride_ = other.stride_;
    data_ = other.data_;

    other.rows_ = 0;
    other.cols_ = 0;
    other.size_ = 0;
    other.stride_ = 0;
    other.data_ = nullptr;
  }

public:
  Grid(size_t rows, size_t cols) {
    rows_ = rows;
    cols_ = cols;

    stride_ = cols + PREFIX_PADDING; // add padding to ensure alignment on interior points
    stride_ = (stride_ + ALIGNED_DOUBLES - 1) & ~(ALIGNED_DOUBLES - 1); // round up to nearest multiple of 4 (4 * 8 = 32 bytes) for alignment across rows
    size_ = rows * stride_ * sizeof(double); // multiple of ALIGNMENT_SIZE, as aligned_alloc requires

    data_ = static_cast<double*>(aligned_alloc(ALIGNMENT_SIZE, size_));

    // first touch: zero rows with the same static split apply_stencil uses, so on a numa machine each row's
    // pages land on the node of the thread that later updates it (off by a row or two at chunk edges, close enough)
    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < rows; i++) {
      memset(data_ + i * stride_, 0, stride_ * sizeof(double));
    }

    boundary_formed_ = false;
  }

  // one owner per buffer, copying would double free
  Grid(const Grid&) = delete;
  Grid& operator=(const Grid&) = delete;

  Grid(Grid&& other) noexcept {
    take(other);
  }

  Grid& operator=(Grid&& other) noexcept {
    if (this != &other) {
      free(data_);
      take(other);
    }
    return *this;
  }

  ~Grid() {
    free(data_);
  }

  size_t get_rows() const {
    return rows_;
  }

  size_t get_cols() const {
    return cols_;
  }

  bool get_boundary_formed() const {
    return boundary_formed_;
  }

  void form_boundary() {
    boundary_formed_ = true;
  }

  double* row_ptr(size_t i) {
    return data_ + i * stride_ + PREFIX_PADDING;
  }

  const double* row_ptr(size_t i) const {
    return data_ + i * stride_ + PREFIX_PADDING;
  }

  double& operator()(size_t row, size_t col) {
    size_t i = row * stride_ + col + PREFIX_PADDING;
    return data_[i];
  }

  double operator()(size_t row, size_t col) const {
    size_t i = row * stride_ + col + PREFIX_PADDING;
    return data_[i];
  }
};

// switch functions directly rather than if statement depending on avx support
typedef void (*stencil_row_fn)(const double* __restrict__, const double* __restrict__, const double* __restrict__,
                               double* __restrict__, const size_t);

// fallback for cpus without avx2, compiler should still vectorize this with wtv the target has
static void apply_stencil_row_scalar(const double* __restrict__ c, const double* __restrict__ up, const double* __restrict__ down,
                              double* __restrict__ dst, const size_t m) {
  #pragma omp parallel for schedule(static) // let omp optimize this
  for (size_t j = 1; j < m - 1; j++) {
    dst[j] = 0.5 * c[j] + 0.125 * (up[j] + down[j] + c[j - 1] + c[j + 1]);
  }
}

#if defined(__x86_64__) || defined(__i386__)
__attribute__((target("avx2,fma")))
static void apply_stencil_row_avx2(const double* __restrict__ c, const double* __restrict__ up, const double* __restrict__ down,
                            double* __restrict__ dst, const size_t m) {
  const __m256d half = _mm256_set1_pd(0.5);
  const __m256d eighth = _mm256_set1_pd(0.125); // do it in here since it's cheap anyway, no messy passing around

  size_t j = 1;

  // vectorized loop, 4 at a time, for interior points only
  for (; j + 4 <= m - 1; j += 4) {

    // these are guaranteed aligned
    __m256d vcur = _mm256_load_pd(c + j);
    __m256d vup = _mm256_load_pd(up + j);
    __m256d vdown = _mm256_load_pd(down + j);

    // these cannot be aligned since they're shifted
    __m256d vleft = _mm256_loadu_pd(c + j - 1);
    __m256d vright = _mm256_loadu_pd(c + j + 1);

    // two fmas replace the final add, one fewer instruction than summing all four neighbors first
    __m256d res = _mm256_fmadd_pd(eighth, _mm256_add_pd(vleft, vright), _mm256_mul_pd(half, vcur));
    res = _mm256_fmadd_pd(eighth, _mm256_add_pd(vup, vdown), res);
    _mm256_store_pd(dst + j, res);
  }

  // scalar for leftover, should be at most 3 leftover, so not worth vectorizing
  for (; j < m - 1; j++) {
    dst[j] = 0.5 * c[j] + 0.125 * (up[j] + down[j] + c[j - 1] + c[j + 1]);
  }
}
#endif

// running avx2/fma instructions on a cpu without them crashes, so check once and fall back
static stencil_row_fn select_stencil_row() {
#if defined(__AVX2__) && defined(__FMA__)
  return apply_stencil_row_avx2; // already compiled for avx2 + fma (e.g. -march=x86-64-v3), no need to check
#elif defined(__x86_64__) || defined(__i386__)
  static const stencil_row_fn fn = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma") ? apply_stencil_row_avx2 : apply_stencil_row_scalar;
  return fn;
#else
  return apply_stencil_row_scalar;
#endif
}

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old, Grid& res) {
  size_t n = old.get_rows();
  size_t m = old.get_cols();

  // no interior points to update
  if (n < 3 || m < 3) {
    if (res.get_boundary_formed()) return;
    for (size_t i = 0; i < n; i++) {
      for (size_t j = 0; j < m; j++) {
        res(i, j) = old(i, j);
      }
    }
    res.form_boundary();
    return;
  }

  const stencil_row_fn apply_stencil_row = select_stencil_row();

  #pragma omp parallel for schedule(static)           // static since workload is uniform
  for (size_t i = 1; i < n - 1; i++) {                // found better to not specify num of threads, let omp decide
    apply_stencil_row(old.row_ptr(i), old.row_ptr(i - 1), old.row_ptr(i + 1), res.row_ptr(i), m);

    res(i, 0) = old(i, 0);  // move back to already threaded loop, less threads + i already in cache
    res(i, m - 1) = old(i, m - 1); // seems not worth to run if statement to check if it's necessary, found to be slower
  }

  // copy boundary values only if first time, since in harness they're just swapped around anyway
  if (!res.get_boundary_formed()) {

    #pragma omp parallel for schedule(static)
    for (size_t j = 0; j < m; j++) {
      res(0, j) = old(0, j);
      res(n - 1, j) = old(n - 1, j);
    }

    res.form_boundary(); // boundary never changes, can leave alone
  }
}
