#pragma once

#include <cstddef>
#include <cstring>
#include <vector>

// grid views are used to simplify API
struct GridView {
    double* data;
    std::size_t rows;
    std::size_t cols;
    std::size_t stride;
};

struct ConstGridView {
    const double* data;
    std::size_t rows;
    std::size_t cols;
    std::size_t stride;
};

// grid class is used to store the grid data and apply the stencil
class Grid {
private:
    std::size_t rows_;
    std::size_t cols_;
    std::vector<double> grid_;

public:
    Grid(std::size_t rows, std::size_t cols) {
        rows_ = rows;
        cols_ = cols;
        grid_ = std::vector<double>(rows_ * cols_);
    }

    double& operator()(std::size_t i, std::size_t j) {
        return grid_[i * cols_ + j];
    }

    double operator()(std::size_t i, std::size_t j) const {
        return grid_[i * cols_ + j];
    }

    std::size_t rows() const {
        return rows_;
    }

    std::size_t cols() const {
        return cols_;
    }

    GridView view() {
        return {
            grid_.data(),
            rows_,
            cols_,
            cols_
        };
    }

    ConstGridView view() const {
        return {
            grid_.data(),
            rows_,
            cols_,
            cols_
        };
    }
};

// copy_boundaries is a helper function that copies the boundaries of the grid to the new grid
static void copy_boundaries(
    ConstGridView old_view,
    GridView new_view
) {
    std::size_t rows = old_view.rows;
    std::size_t cols = old_view.cols;
    std::size_t stride = old_view.stride;

    std::memcpy(
        new_view.data,
        old_view.data,
        cols * sizeof(double)
    );

    std::memcpy(
        new_view.data + (rows - 1) * stride,
        old_view.data + (rows - 1) * stride,
        cols * sizeof(double)
    );

    for (std::size_t i = 1; i < rows - 1; i++) {
        new_view.data[i * stride] =
            old_view.data[i * stride];

        new_view.data[i * stride + cols - 1] =
            old_view.data[i * stride + cols - 1];
    }
}

// stencil_row is a helper function that applies the stencil to a single row of the grid
static void stencil_row(
    const double* __restrict old_data,
    double* __restrict new_data,
    std::size_t i,
    std::size_t cols,
    std::size_t stride
) {
    const double* top = old_data + (i - 1) * stride;
    const double* mid = old_data + i * stride;
    const double* bottom = old_data + (i + 1) * stride;
    double* out = new_data + i * stride;

    // vectorization report shows that we are achieving vectorization on this loop with 16 byte vectors (local machine)
    #pragma omp simd
    for (std::size_t j = 1; j < cols - 1; j++) {
        out[j] =
            0.5 * mid[j] +
            0.125 * (
                top[j] +
                bottom[j] +
                mid[j - 1] +
                mid[j + 1]
            );
    }
}

// apply_stencil is the main function that applies the stencil to the grid
inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
    ConstGridView old_view = old_grid.view();
    GridView new_view = new_grid.view();

    const std::size_t rows = old_view.rows;
    const std::size_t cols = old_view.cols;
    const std::size_t stride = old_view.stride;

    if (rows == 0 || cols == 0) {
        return;
    }

    copy_boundaries(old_view, new_view);

    // local machine 8 threads found to be optimal but could vary across different machines
    #pragma omp parallel for schedule(static)

    // optimal number of rows to begin parallelization depends on machine and on profiling
    for (std::size_t i = 1; i < rows - 1; i++) {
        stencil_row(
            old_view.data,
            new_view.data,
            i,
            cols,
            stride
        );
    }
}