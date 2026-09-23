#pragma once
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <memory>
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
    return static_cast<T*>(__builtin_assume_aligned(ptr,Alignement));
}


class Grid {
private:
    std::size_t rows_;
    std::size_t cols_;
    std::size_t row_width_;

    double* data_;

    static constexpr std::size_t align= 64;
    static constexpr std::size_t elements_per_vec= 64/sizeof(double);
public:
    Grid(std::size_t rows, std::size_t cols)
        : rows_(rows), cols_(cols) {
        row_width_ = ((cols + elements_per_vec - 1) / elements_per_vec) * elements_per_vec;

        std::size_t total_elements = rows_ * row_width_;
        std::size_t total_bytes = total_elements * sizeof(double);

        void* ptr= std::aligned_alloc(align,total_bytes);
        data_= static_cast<double*>(ptr);
    };

    double& operator()(std::size_t i, std::size_t j) {
        return data_[i*row_width_ + j];
    }
    double  operator()(std::size_t i, std::size_t j) const {
        return data_[i*row_width_ + j];
    }
    [[nodiscard]] size_t row_size() const {return rows_;};
    [[nodiscard]] size_t col_size() const {return cols_;};
    [[nodiscard]] size_t row_width() const {return row_width_;};
    /// access data
    [[nodiscard]] double* data(){return assume_aligned<align>(data_);};
    [[nodiscard]] const double* data() const {return assume_aligned<align>(data_);}

    /// destruct
    ~Grid() {std::free(data_);}
    Grid(const Grid&) = delete;
    Grid& operator=(const Grid&) = delete;
};

// Top and bottom rows only, left/right columns are copied inside the stencil loop while hot on L1
inline void copy_top_bottom(const Grid& old_grid, Grid& new_grid,
                            const std::size_t rows, const std::size_t row_width)
{
    const double* __restrict src = old_grid.data();
    double* __restrict dst = new_grid.data();
    const std::size_t bytes = row_width * sizeof(double);
    const std::size_t last  = row_width * (rows - 1);

    std::memcpy(dst, src, bytes);
    std::memcpy(dst + last, src + last, bytes);
}

void apply_stencil(const Grid& old_grid, Grid& new_grid) {
    const std::size_t rows= old_grid.row_size();
    const std::size_t cols= old_grid.col_size();
    const size_t row_width= old_grid.row_width();

    const double* __restrict src= assume_aligned<64>(old_grid.data());
    double* __restrict dst= assume_aligned<64>(new_grid.data());

    pin_threads_once(); // assign a thread to a core
    #pragma omp parallel for schedule(static)

    for (size_t r = 1; r < rows - 1; r++) {
    const double* __restrict src_curr = src + (r * row_width);
    const double* __restrict src_prev = src + ((r - 1) * row_width);
    const double* __restrict src_next = src + ((r + 1) * row_width);
    double* __restrict dst_curr       = dst + (r * row_width);

    #pragma omp simd aligned(src_curr, src_prev, src_next,dst_curr:64)
    for (size_t c = 1; c < cols - 1; c++) {
        dst_curr[c] = 0.5   * src_curr[c] +
                      0.125 * (src_prev[c] + src_next[c] +
                               src_curr[c + 1] + src_curr[c - 1]);
    }
    // left/right borders both values were just read by the loop above, so they're still in L1
    dst_curr[0]        = src_curr[0];
    dst_curr[cols - 1] = src_curr[cols - 1];
}
    /// top/bottom border
    copy_top_bottom(old_grid, new_grid, rows, row_width);
}
