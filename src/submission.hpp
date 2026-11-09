#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <omp.h>
#include <pthread.h>
#include <sched.h>

// pin threads to a cpu core
// on re-reads previous values will be in higher caches
inline void pin_threads_once() {
    static bool done = false; // threads are assigned once, no re-pinning overhead
    if (done) return;
    done = true;

    cpu_set_t allowed;
    if (sched_getaffinity(0, sizeof allowed, &allowed) != 0) return;

    #pragma omp parallel
    {
        int target = omp_get_thread_num() % CPU_COUNT(&allowed);
        for (int c = 0; c < CPU_SETSIZE; ++c) {
            if (CPU_ISSET(c, &allowed) && target-- == 0) {
                cpu_set_t one;
                CPU_ZERO(&one);
                CPU_SET(c, &one);
                pthread_setaffinity_np(pthread_self(), sizeof one, &one);
                break;
            }
        }
    }
}

template <std::size_t Alignement, typename T>
static constexpr T* assume_aligned(T* ptr) noexcept {
    return static_cast<T*>(__builtin_assume_aligned(ptr, Alignement));
}

// Inclusive rectangle [r0, r1] x [c0, c1]. Empty when r0 > r1 or c0 > c1.
struct Rect {
    long r0, r1, c0, c1;

    static constexpr Rect empty() { return {1, 0, 1, 0}; }
    [[nodiscard]] bool is_empty() const { return r0 > r1 || c0 > c1; }

    // grow by n cells on every side, then clip to [0,rows) x [0,cols)
    [[nodiscard]] Rect expand_clip(long n, long rows, long cols) const {
        if (is_empty()) return *this;
        return {std::max(r0 - n, 0L), std::min(r1 + n, rows - 1),
                std::max(c0 - n, 0L), std::min(c1 + n, cols - 1)};
    }
    // smallest rectangle containing both
    [[nodiscard]] Rect hull(const Rect& o) const {
        if (is_empty()) return o;
        if (o.is_empty()) return *this;
        return {std::min(r0, o.r0), std::max(r1, o.r1),
                std::min(c0, o.c0), std::max(c1, o.c1)};
    }
};

class Grid {
private:
    std::size_t rows_;
    std::size_t cols_;
    std::size_t row_width_;

    double* data_;

    mutable Rect bounds_ = Rect::empty();
    mutable bool bounds_known_ = true;

    static constexpr std::size_t align = 64;
    static constexpr std::size_t elements_per_vec = 64 / sizeof(double);

public:
    Grid(std::size_t rows, std::size_t cols)
        : rows_(rows), cols_(cols) {
        row_width_ = ((cols + elements_per_vec - 1) / elements_per_vec) * elements_per_vec;

        const std::size_t total_bytes = rows_ * row_width_ * sizeof(double);
        data_ = static_cast<double*>(std::aligned_alloc(align, total_bytes));
        if (!data_) throw std::bad_alloc{};

        // Zeroing is what makes an empty bounding rect true for a fresh grid.
        // Done in parallel, same static row split as the kernel (NUMA first touch).
        // Runs in the constructor, outside the timed loop.
        const long n = static_cast<long>(rows_);
        #pragma omp parallel for schedule(static)
        for (long r = 0; r < n; ++r)
            std::memset(data_ + r * row_width_, 0, row_width_ * sizeof(double));
    }

    double& operator()(std::size_t i, std::size_t j) {
        bounds_known_ = false; // caller may write anything here
        return data_[i * row_width_ + j];
    }
    double operator()(std::size_t i, std::size_t j) const {
        return data_[i * row_width_ + j];
    }
    [[nodiscard]] size_t row_size() const { return rows_; }
    [[nodiscard]] size_t col_size() const { return cols_; }
    [[nodiscard]] size_t row_width() const { return row_width_; }
    /// access data
    [[nodiscard]] double* data() { return assume_aligned<align>(data_); }
    [[nodiscard]] const double* data() const { return assume_aligned<align>(data_); }

    /// bounding rect of the possibly-nonzero cells (scans once if unknown)
    [[nodiscard]] Rect nonzero_bounds() const {
        if (!bounds_known_) {
            bounds_ = scan_nonzero();
            bounds_known_ = true;
        }
        return bounds_;
    }
    void set_nonzero_bounds(const Rect& r) {
        bounds_ = r;
        bounds_known_ = true;
    }

    /// destruct
    ~Grid() { std::free(data_); }
    Grid(const Grid&) = delete;
    Grid& operator=(const Grid&) = delete;

private:
    // One full pass, parallel min/max reduction. NaN != 0 so it counts as nonzero.
    [[nodiscard]] Rect scan_nonzero() const {
        const long rows = static_cast<long>(rows_);
        const long cols = static_cast<long>(cols_);
        long r0 = rows, r1 = -1, c0 = cols, c1 = -1;

        #pragma omp parallel for schedule(static) reduction(min:r0, c0) reduction(max:r1, c1)
        for (long r = 0; r < rows; ++r) {
            const double* row = data_ + r * row_width_;
            long first = 0;
            while (first < cols && row[first] == 0.0) ++first;
            if (first == cols) continue; // all zero row
            long last = cols - 1;
            while (row[last] == 0.0) --last;
            r0 = std::min(r0, r);
            r1 = std::max(r1, r);
            c0 = std::min(c0, first);
            c1 = std::max(c1, last);
        }
        if (r1 < 0) return Rect::empty();
        return {r0, r1, c0, c1};
    }
};

// A cell whose value and 4 neighbours are all 0 becomes 0.5*0 + 0.125*0 = 0, so if
// old_grid is zero outside rect B, new_grid is zero outside B grown by 1 cell
// (border cells are copied, so they stay inside B). We only need to do work on
// that grown rect, plus any cells that were nonzero in new_grid's stale
// contents (from two steps ago) so they get overwritten with the right value.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {
    const long rows = static_cast<long>(old_grid.row_size());
    const long cols = static_cast<long>(old_grid.col_size());
    const long row_width = static_cast<long>(old_grid.row_width());

    const Rect stale = new_grid.nonzero_bounds();               // what dst still holds
    const Rect next  = old_grid.nonzero_bounds().expand_clip(1, rows, cols); // what dst will hold
    const Rect work  = next.hull(stale);                        // cells to write

    if (!work.is_empty()) {
        const double* __restrict src = assume_aligned<64>(old_grid.data());
        double* __restrict dst = assume_aligned<64>(new_grid.data());

        // interior part of the work rect
        const long ir0 = std::max(work.r0, 1L), ir1 = std::min(work.r1, rows - 2);
        const long ic0 = std::max(work.c0, 1L), ic1 = std::min(work.c1, cols - 2);
        const bool do_left  = work.c0 == 0;
        const bool do_right = work.c1 == cols - 1 && cols > 1;

        pin_threads_once(); // assign a thread to a core
        #pragma omp parallel for schedule(static)
        for (long r = ir0; r <= ir1; ++r) {
            const double* __restrict src_curr = src + r * row_width;
            const double* __restrict src_prev = src_curr - row_width;
            const double* __restrict src_next = src_curr + row_width;
            double* __restrict dst_curr       = dst + r * row_width;

            #pragma omp simd aligned(src_curr, src_prev, src_next, dst_curr : 64)
            for (long c = ic0; c <= ic1; ++c) {
                dst_curr[c] = 0.5   * src_curr[c] +
                              0.125 * (src_prev[c] + src_next[c] +
                                       src_curr[c + 1] + src_curr[c - 1]);
            }
            // left/right borders, only if the rect touches them; still hot in L1
            if (do_left)  dst_curr[0]        = src_curr[0];
            if (do_right) dst_curr[cols - 1] = src_curr[cols - 1];
        }

        // top/bottom border rows, only the columns inside the work rect
        const std::size_t bytes = static_cast<std::size_t>(work.c1 - work.c0 + 1) * sizeof(double);
        if (work.r0 == 0) {
            std::memcpy(dst + work.c0, src + work.c0, bytes);
        }
        if (work.r1 == rows - 1 && rows > 1) {
            const long off = (rows - 1) * row_width + work.c0;
            std::memcpy(dst + off, src + off, bytes);
        }
    }

    // Cells in work but outside next were just computed as exactly 0, and cells
    // outside work were already 0, so the invariant holds for `next`.
    new_grid.set_nonzero_bounds(next);
}
