#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>

struct Dim {
    std::size_t rows;
    std::size_t cols;

    constexpr bool operator==(const Dim& other) const noexcept {
        return rows == other.rows && cols == other.cols;
    }
};

template <typename T>
struct View {
    T* cells;
    Dim dim;
    std::size_t stride;

    T& operator()(std::size_t row, std::size_t col) const {
        return cells[row * stride + col];
    }
};

using GridView = View<double>;
using ConstGridView = View<const double>;

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
   private:
    static constexpr std::size_t alignVal = 64;

    static std::size_t calc_stride(std::size_t cols) {
        static_assert(alignVal % sizeof(double) == 0);
        constexpr std::size_t width = alignVal / sizeof(double);
        if (cols > std::numeric_limits<std::size_t>::max() - (width - 1)) {
            throw std::length_error("width too large for stride");
        }
        return ((cols + width - 1) / width) * width;
    }

    static std::size_t storage_size(std::size_t rows, std::size_t stride) {
        if (stride != 0 &&
            rows > std::numeric_limits<std::size_t>::max() / stride) {
            throw std::length_error("exceeded maximum storage size");
        }
        const std::size_t count = rows * stride;
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(double)) {
            throw std::length_error("exceeded maximum storage size");
        }
        return count;
    }

    Dim dim_;
    std::size_t stride_;
    double* data_;

   public:
    Grid(std::size_t rows, std::size_t cols)
        : dim_{rows, cols}, stride_(calc_stride(cols)), data_(nullptr) {
        const std::size_t count = storage_size(rows, stride_);
        if (count == 0) return;

        data_ = static_cast<double*>(
            std::aligned_alloc(alignVal, count * sizeof(double)));

        if (data_ == nullptr) throw std::bad_alloc();
        std::fill_n(data_, count, 0.0);
    }

    Grid(const Grid&) = delete;
    Grid& operator=(const Grid&) = delete;

    ~Grid() { std::free(data_); }

    double& operator()(std::size_t i, std::size_t j) {
        assert(i < dim_.rows && j < dim_.cols);
        return data_[i * stride_ + j];
    }
    double operator()(std::size_t i, std::size_t j) const {
        assert(i < dim_.rows && j < dim_.cols);
        return data_[i * stride_ + j];
    }

    Dim dim() const { return dim_; }
    std::size_t rows() const { return dim_.rows; }
    std::size_t cols() const { return dim_.cols; }
    std::size_t stride() const { return stride_; }

    double* data() { return data_; }
    const double* data() const { return data_; }

    GridView view() { return {data_, dim_, stride_}; }
    ConstGridView view() const { return {data_, dim_, stride_}; }
};

inline void compute_interior(ConstGridView old_view, GridView new_view) {
    const std::size_t rows = old_view.dim.rows;
    const std::size_t cols = old_view.dim.cols;
    const double* __restrict__ old_data = old_view.cells;
    double* __restrict__ new_data = new_view.cells;
    const std::size_t old_stride = old_view.stride;
    const std::size_t new_stride = new_view.stride;

#pragma omp parallel for schedule(static)
    for (std::size_t i = 1; i < rows - 1; i++) {
        const double* old_row = old_data + i * old_stride;
        const double* old_above = old_row - old_stride;
        const double* old_below = old_row + old_stride;
        double* new_row = new_data + i * new_stride;

        new_row[0] = old_row[0];
        new_row[cols - 1] = old_row[cols - 1];
#pragma omp simd
        for (std::size_t j = 1; j < cols - 1; j++) {
            new_row[j] =
                0.5 * old_row[j] + 0.125 * (old_above[j] + old_below[j] +
                                            old_row[j - 1] + old_row[j + 1]);
        }
    }
}

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
    assert(&old_grid != &new_grid &&
           "old_grid and new_grid must not share the same memory");
    assert(old_grid.dim() == new_grid.dim() &&
           "old_grid and new_grid must have the same dimensions");

    const ConstGridView old_view = old_grid.view();
    const GridView new_view = new_grid.view();
    const std::size_t rows = old_view.dim.rows;
    const std::size_t cols = old_view.dim.cols;

    if (rows == 0 || cols == 0) return;

    if (rows < 3 || cols < 3) {
        for (std::size_t i = 0; i < rows; ++i) {
            std::copy_n(old_view.cells + i * old_view.stride, cols,
                        new_view.cells + i * new_view.stride);
        }
        return;
    }

    std::copy_n(old_view.cells, cols, new_view.cells);
    std::copy_n(old_view.cells + (rows - 1) * old_view.stride, cols,
                new_view.cells + (rows - 1) * new_view.stride);
    compute_interior(old_view, new_view);
}