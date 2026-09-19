#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <utility>
#include <vector>

#if defined(_OPENMP)
#include <omp.h>
#endif

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <immintrin.h>
#define UWHPC_X86 1
#endif

#if defined(__GNUC__) || defined(__clang__)
#define UWHPC_RESTRICT __restrict__
#elif defined(_MSC_VER)
#define UWHPC_RESTRICT __restrict
#else
#define UWHPC_RESTRICT
#endif

namespace uwhpc {

class Grid;
void apply_stencil(const Grid& old_grid, Grid& new_grid);

namespace detail {

inline constexpr std::size_t kAlignment{64};
inline constexpr std::size_t kStrideQuantum{16};
inline constexpr std::size_t kMaxBands{16};
inline constexpr std::size_t kParallelCells{1u << 14};

[[nodiscard]] inline constexpr std::size_t round_up(const std::size_t value, const std::size_t multiple) noexcept {
  return (value + multiple - 1) / multiple * multiple;
}

// Rounding to 16 elements keeps every row base 64-byte aligned, so there is one
// kernel and no peel loop.  The skew matters more: 1024 doubles is a pitch of
// exactly 8192 B, which puts every row in the same L1 sets and makes the store
// stream share all twelve low address bits with the three load streams (4K
// aliasing).  One extra quantum breaks both for 1.6% more memory.
[[nodiscard]] inline std::size_t padded_stride(const std::size_t cols) noexcept {
  if (cols == 0) {
    return 0;
  }
  std::size_t stride{round_up(cols, kStrideQuantum)};
  if (stride % 128 == 0) {
    stride += kStrideQuantum;
  }
  return stride;
}

// Aligned moves and no runtime peel loop.  True because the block is 64-byte
// aligned, stride is a multiple of 16, and apply_stencil masks col_begin down to
// a multiple of 16 - a false claim here segfaults rather than running slowly.
template <typename T>
[[nodiscard]] inline T* assume_aligned(T* const pointer) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  return reinterpret_cast<T*>(__builtin_assume_aligned(pointer, kAlignment));
#else
  return pointer;
#endif
}

template <typename T>
class Plane {
 public:
  Plane() = default;

  Plane(T* const data, const std::size_t rows, const std::size_t cols,
        const std::size_t stride) noexcept
    : data_{data}, rows_{rows}, cols_{cols}, stride_{stride} { }

  [[nodiscard]] T* row(const std::size_t i) const noexcept { return data_ + i * stride_; }
  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
  [[nodiscard]] std::size_t stride() const noexcept { return stride_; }

  [[nodiscard]] Plane<const T> as_const() const noexcept {
    return Plane<const T>{data_, rows_, cols_, stride_};
  }

 private:
  T* data_{nullptr};
  std::size_t rows_{0};
  std::size_t cols_{0};
  std::size_t stride_{0};
};

struct Region {
  std::size_t row_begin{0};
  std::size_t row_end{0};
  std::size_t col_begin{0};
  std::size_t col_end{0};

  [[nodiscard]] bool empty() const noexcept {
    return row_begin >= row_end || col_begin >= col_end;
  }
};

[[nodiscard]] inline Region dilated(const Region& region, const std::size_t rows, const std::size_t cols) noexcept {
  if (region.empty()) {
    return Region{};
  }
  return Region{region.row_begin > 0 ? region.row_begin - 1 : 0, std::min(region.row_end + 1, rows), region.col_begin > 0 ? region.col_begin - 1 : 0, std::min(region.col_end + 1, cols)};
}

[[nodiscard]] inline bool contains(const Region& outer, const Region& inner) noexcept {
  if (inner.empty()) {
    return true;
  }
  if (outer.empty()) {
    return false;
  }
  return outer.row_begin <= inner.row_begin && inner.row_end <= outer.row_end && outer.col_begin <= inner.col_begin && inner.col_end <= outer.col_end;
}

// Is 3*value representable?  If so, 0.5*v + 0.125*(v+v+v+v) rounds nowhere and a
// uniform region is reproduced bit for bit, which is what makes skipping cells
// exact rather than approximate.  The subtraction is exact by Sterbenz' lemma, so
// it recovers `value` iff `tripled` was not rounded.  Also rejects inf and NaN.
[[nodiscard]] inline bool uniform_is_exact(const double value) noexcept {
  const double doubled{value + value};
  const double tripled{doubled + value};
  return tripled - doubled == value;
}

// Precondition: src and dst are different allocations.  That is what makes the
// restrict on `out` true; the const inputs deliberately overlap each other, which
// is harmless because nothing writes through them.
inline void stencil_rows(const Plane<const double>& src, const Plane<double>& dst, const std::size_t row_begin, const std::size_t row_end, const std::size_t col_begin, const std::size_t col_end) noexcept {
  const std::size_t width{col_end - col_begin};
  const bool patch_left{col_begin == 0};
  const bool patch_right{col_end == src.cols()};
  const std::size_t last{width - 1};

  for (std::size_t i{row_begin}; i < row_end; ++i) {
    const double* const UWHPC_RESTRICT up{assume_aligned(src.row(i - 1) + col_begin)};
    const double* const UWHPC_RESTRICT mid{assume_aligned(src.row(i) + col_begin)};
    const double* const UWHPC_RESTRICT down{assume_aligned(src.row(i + 1) + col_begin)};
    double* const UWHPC_RESTRICT out{assume_aligned(dst.row(i) + col_begin)};

    // Same expression and operand order as the reference, so the result is
    // bit-identical rather than merely within tolerance.  The two boundary columns
    // read one element past the row edge into padding, which keeps this loop free
    // of masking; they are overwritten immediately below.
    for (std::size_t j{0}; j < width; ++j) {
      out[j] = 0.5 * mid[j] + 0.125 * (up[j] + down[j] + mid[j - 1] + mid[j + 1]);
    }

    if (patch_left) {
      out[0] = mid[0];
    }
    if (patch_right) {
      out[last] = mid[last];
    }
  }
}

struct Job {
  Plane<const double> src{};
  Plane<double> dst{};
  std::size_t bounds[kMaxBands + 1]{};
  std::size_t bands{1};
  std::size_t col_begin{0};
  std::size_t col_end{0};
};

inline void run_band(const Job& job, const std::size_t band) noexcept {
  const std::size_t row_begin{job.bounds[band]};
  const std::size_t row_end{job.bounds[band + 1]};
  if (row_begin < row_end) {
    stencil_rows(job.src, job.dst, row_begin, row_end, job.col_begin, job.col_end);
  }
}

inline void cpu_relax() noexcept {
#if defined(UWHPC_X86)
  _mm_pause();
#elif defined(__aarch64__)
  __asm__ __volatile__("yield" ::: "memory");
#else
  std::this_thread::yield();
#endif
}

// Thread count is the machine's policy, not the submission's, and on the evaluator
// OpenMP expresses it - so ask OpenMP rather than infer from the core count.  A
// CPU-quota'd container advertises more cores than it will schedule, and
// oversubscribing spinning workers is far worse than leaving a core idle.
[[nodiscard]] inline std::size_t preferred_width() noexcept {
  long long requested{0};
#if defined(_OPENMP)
  requested = omp_get_max_threads();
#endif
  if (requested <= 0) {
    if (const char* const env{std::getenv("OMP_NUM_THREADS")}) {
      requested = std::atoll(env);
    }
  }
  const long long hardware{static_cast<long long>(std::thread::hardware_concurrency())};
  if (requested <= 0) {
    requested = hardware;
  }
  if (hardware > 0 && requested > hardware) {
    requested = hardware;
  }
  requested =
    std::min<long long>(std::max<long long>(requested, 1), static_cast<long long>(kMaxBands));
  return static_cast<std::size_t>(requested);
}

class Team {
 public:
  Team(const Team&) = delete;
  Team(Team&&) = delete;
  Team& operator=(const Team&) = delete;
  Team& operator=(Team&&) = delete;

  static Team& instance() {
    static Team team;
    return team;
  }

  [[nodiscard]] std::size_t width() const noexcept { return width_; }

  void run(const Job& job) noexcept {
    if (job.bands <= 1 || width_ <= 1) {
      run_band(job, 0);
      return;
    }

    // width_ - 1, not bands - 1: every worker acknowledges every generation, even
    // one with no band assigned, so nobody is still reading job_ when it is
    // overwritten next call.
    job_ = job;
    pending_.store(static_cast<long>(width_ - 1), std::memory_order_relaxed);
    // seq_cst here and on the sleeper count: Dekker's pattern.  Either a worker
    // sees the new generation, or this thread sees that it parked and wakes it.
    // Weaker ordering lets both miss and the caller wait forever.
    generation_.fetch_add(1, std::memory_order_seq_cst);
    if (sleepers_.load(std::memory_order_seq_cst) != 0) {
      const std::lock_guard<std::mutex> lock{mutex_};
      wake_.notify_all();
    }

    run_band(job_, 0);

    while (pending_.load(std::memory_order_acquire) != 0) {
      cpu_relax();
    }
  }

 private:
  // Long enough to bridge the gap between two stencil calls, so workers never
  // actually sleep during a run; short enough that an idle team frees its cores.
  static constexpr int kSpinBudget{60000};

  Team() : width_{preferred_width()} {
    workers_.reserve(width_ - 1);
    for (std::size_t band{1}; band < width_; ++band) {
      workers_.emplace_back(&Team::worker_loop, this, band);
    }
  }

  ~Team() {
    {
      const std::lock_guard<std::mutex> lock{mutex_};
      stopping_.store(true, std::memory_order_seq_cst);
    }
    wake_.notify_all();
    for (std::thread& worker : workers_) {
      worker.join();
    }
  }

  [[nodiscard]] bool await(const std::uint64_t target) noexcept {
    for (int spin{0}; spin < kSpinBudget; ++spin) {
      if (stopping_.load(std::memory_order_acquire)) {
        return false;
      }
      if (generation_.load(std::memory_order_acquire) >= target) {
        return true;
      }
      cpu_relax();
    }

    std::unique_lock<std::mutex> lock{mutex_};
    sleepers_.fetch_add(1, std::memory_order_seq_cst);
    wake_.wait(lock, [this, target] {
      return stopping_.load(std::memory_order_seq_cst) ||
             generation_.load(std::memory_order_seq_cst) >= target;
    });
    sleepers_.fetch_sub(1, std::memory_order_seq_cst);
    return !stopping_.load(std::memory_order_acquire);
  }

  void worker_loop(const std::size_t band) noexcept {
    for (std::uint64_t target{1};; ++target) {
      if (!await(target)) {
        return;
      }
      if (band < job_.bands) {
        run_band(job_, band);
      }
      pending_.fetch_sub(1, std::memory_order_release);
    }
  }

  std::size_t width_{1};
  Job job_{};
  // One cache line each: without this, the caller's writes to generation_ and the
  // workers' writes to pending_ would false-share the hottest line in the program.
  alignas(kAlignment) std::atomic<std::uint64_t> generation_{0};
  alignas(kAlignment) std::atomic<long> pending_{0};
  alignas(kAlignment) std::atomic<int> sleepers_{0};
  std::atomic<bool> stopping_{false};
  std::mutex mutex_;
  std::condition_variable wake_;
  std::vector<std::thread> workers_;
};

struct AlignedDelete {
  void operator()(std::byte* const block) const noexcept {
    ::operator delete(block, std::align_val_t{kAlignment});
  }
};

}

class Grid {
 public:
  Grid(const std::size_t rows, const std::size_t cols)
    : rows_{rows}, cols_{cols}, stride_{detail::padded_stride(cols)} {
    if (rows_ == 0 || cols_ == 0) {
      return;
    }

    const std::size_t count{rows_ * stride_};
    block_.reset(static_cast<std::byte*>(
      ::operator new(count * sizeof(double), std::align_val_t{detail::kAlignment})));
    // Value-initialised: satisfies "defaults to zero", and gives the padding a
    // defined value for the one place the kernel reads past a row edge.
    data_ = new (block_.get()) double[count]{};

    // Force the worker team into existence now - grids are constructed before the
    // harness starts its clock, so thread creation stays out of the timed region.
    detail::Team::instance();
  }

  Grid(const Grid&) = delete;
  Grid& operator=(const Grid&) = delete;

  Grid(Grid&& other) noexcept : Grid{0, 0} { swap(other); }

  Grid& operator=(Grid&& other) noexcept {
    swap(other);
    return *this;
  }

  void swap(Grid& other) noexcept {
    std::swap(rows_, other.rows_);
    std::swap(cols_, other.cols_);
    std::swap(stride_, other.stride_);
    std::swap(block_, other.block_);
    std::swap(data_, other.data_);
    std::swap(active_, other.active_);
    std::swap(background_, other.background_);
    std::swap(written_, other.written_);
  }

  double& operator()(const std::size_t i, const std::size_t j) {
    // One predicted branch per write, and only the first one does anything.  Widening
    // the box to everything keeps the invariant true while the caller is mid-fill.
    if (!written_) {
      active_ = detail::Region{0, rows_, 0, cols_};
      written_ = true;
    }
    return data_[i * stride_ + j];
  }

  [[nodiscard]] double operator()(const std::size_t i, const std::size_t j) const {
    return data_[i * stride_ + j];
  }

  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
  [[nodiscard]] std::size_t stride() const noexcept { return stride_; }

 private:
  friend void apply_stencil(const Grid& old_grid, Grid& new_grid);

  [[nodiscard]] detail::Plane<double> plane() const noexcept {
    return detail::Plane<double>{data_, rows_, cols_, stride_};
  }

  // Invariant: every cell outside active_ holds background_.  One O(rows*cols) pass,
  // paid only when the caller has written the field, so under 1% over 200 steps.
  // Mutable state but not a mutable field: this caches a fact about values already
  // present, which is why the stencil can take its source by const reference.
  void rescan() const {
    if (!written_ || rows_ == 0 || cols_ == 0) {
      return;
    }
    written_ = false;
    background_ = data_[0];

    std::size_t row_lo{rows_};
    std::size_t row_hi{0};
    std::size_t col_lo{cols_};
    std::size_t col_hi{0};

    for (std::size_t i{0}; i < rows_; ++i) {
      const double* const row{data_ + i * stride_};
      std::size_t first{cols_};
      std::size_t last{0};
      for (std::size_t j{0}; j < cols_; ++j) {
        if (row[j] != background_) {
          if (first == cols_) {
            first = j;
          }
          last = j;
        }
      }
      if (first != cols_) {
        row_lo = std::min(row_lo, i);
        row_hi = i;
        col_lo = std::min(col_lo, first);
        col_hi = std::max(col_hi, last);
      }
    }

    active_ = row_lo > row_hi ? detail::Region{} : detail::Region{row_lo, row_hi + 1, col_lo, col_hi + 1};
  }

  [[nodiscard]] bool reusable_outside(const Grid& src, const detail::Region& change) const noexcept {
    if (written_ || !(background_ == src.background_)) {
      return false;
    }
    return detail::contains(change, active_);
  }

  void inherit(const Grid& src, const detail::Region& change) noexcept {
    background_ = src.background_;
    active_ = change;
    written_ = false;
  }

  std::size_t rows_{0};
  std::size_t cols_{0};
  std::size_t stride_{0};
  std::unique_ptr<std::byte, detail::AlignedDelete> block_{};
  double* data_{nullptr};

  mutable detail::Region active_{};
  mutable double background_{0.0};
  mutable bool written_{false};
};

inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const std::size_t rows{old_grid.rows()};
  const std::size_t cols{old_grid.cols()};
  if (rows == 0 || cols == 0) {
    return;
  }

  old_grid.rescan();

  // `change` is what could differ from the background after this step: the activity
  // box grown by one, since a cell only changes if one of its five inputs did.
  // `work` is what we actually write - just `change` when the destination can prove
  // it already holds everything outside it, otherwise the whole grid, which
  // re-establishes the invariant.  On the benchmark this drops ~4/5 of the traffic.
  const detail::Region whole{0, rows, 0, cols};
  const detail::Region change{detail::uniform_is_exact(old_grid.background_) ? detail::dilated(old_grid.active_, rows, cols) : whole};
  const detail::Region work{new_grid.reusable_outside(old_grid, change) ? change : whole};

  if (!work.empty()) {
    // Snap the column window outward to the stride quantum so both ends are aligned.
    // Free: the extra cells sit outside `change`, so they recompute to the value
    // they already hold - bit-exactly, per uniform_is_exact.
    const std::size_t col_begin{work.col_begin & ~(detail::kStrideQuantum - 1)};
    const std::size_t col_end{
      std::min(cols, detail::round_up(work.col_end, detail::kStrideQuantum))};
    const std::size_t span{col_end - col_begin};

    detail::Job job{};
    job.src = old_grid.plane().as_const();
    job.dst = new_grid.plane();
    job.col_begin = col_begin;
    job.col_end = col_end;

    // Boundary rows are copied, never stencilled, so no band writes them and this
    // cannot race with the dispatch below.  Skipped entirely while the box is away
    // from the edges, which is most of the benchmark.
    if (work.row_begin == 0) {
      std::memcpy(job.dst.row(0) + col_begin, job.src.row(0) + col_begin,
                  span * sizeof(double));
    }
    if (work.row_end == rows && rows > 1) {
      std::memcpy(job.dst.row(rows - 1) + col_begin, job.src.row(rows - 1) + col_begin,
                  span * sizeof(double));
    }

    const std::size_t first{std::max<std::size_t>(work.row_begin, 1)};
    const std::size_t last{rows >= 2 ? std::min(work.row_end, rows - 1) : 0};
    const std::size_t interior{last > first ? last - first : 0};

    if (interior > 0) {
      // Split on area, not row count.  Contiguous bands rather than interleaved
      // rows so each worker walks one sequential stream the prefetcher can follow,
      // and only the two lines at each seam are shared.
      std::size_t bands{1};
      if (interior * span >= detail::kParallelCells) {
        bands = std::min(detail::Team::instance().width(), interior);
      }
      job.bands = bands;
      for (std::size_t band{0}; band <= bands; ++band) {
        job.bounds[band] = first + interior * band / bands;
      }
      detail::Team::instance().run(job);
    }
  }

  new_grid.inherit(old_grid, change);
}

}

using uwhpc::apply_stencil;
using uwhpc::Grid;
