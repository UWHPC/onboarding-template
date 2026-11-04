#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>

class Grid {
private:
    std::size_t rows_;
    std::size_t cols_;
    std::size_t stride_;
    std::vector<double> data_;

public:
    Grid(std::size_t rows, std::size_t cols)
        : rows_(rows),
          cols_(cols),
          stride_(cols),
          data_(rows * stride_, 0.0)
    {}

    // Read/write access to a cell
    double& operator()(std::size_t i, std::size_t j) {
        return data_[i * stride_ + j];
    }

    // Read-only access to a cell
    double operator()(std::size_t i, std::size_t j) const {
        return data_[i * stride_ + j];
    }

    std::size_t rows() const {
        return rows_;
    }

    std::size_t cols() const {
        return cols_;
    }

    std::size_t stride() const {
        return stride_;
    }

    // Pointer to the beginning of a mutable row
    double* row_data(std::size_t i) {
        return data_.data() + i * stride_;
    }

    // Pointer to the beginning of a read-only row
    const double* row_data(std::size_t i) const {
        return data_.data() + i * stride_;
    }
};

inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
    assert(old_grid.rows() == new_grid.rows());
    assert(old_grid.cols() == new_grid.cols());
    assert(&old_grid != &new_grid);

    const std::size_t rows = old_grid.rows();
    const std::size_t cols = old_grid.cols();

    // Empty grids contain no cells to update
    if (rows == 0 || cols == 0) {
        return;
    }

    // Tiny grids have no interior cells
    // Every cell is part of the boundary
    if (rows < 3 || cols < 3) {
        for (std::size_t i = 0; i < rows; ++i) {
            for (std::size_t j = 0; j < cols; ++j) {
                new_grid(i, j) = old_grid(i, j);
            }
        }
        return;
    }

    // Copy the top boundary row
    std::copy_n(
        old_grid.row_data(0),
        cols,
        new_grid.row_data(0)
    );

    // Copy the bottom boundary row
    std::copy_n(
        old_grid.row_data(rows - 1),
        cols,
        new_grid.row_data(rows - 1)
    );

    // Distribute independent interior rows across threads
    #pragma omp parallel for schedule(static)
    for (std::size_t i = 1; i < rows - 1; ++i) {

        const double* north  = old_grid.row_data(i - 1);
        const double* center = old_grid.row_data(i);
        const double* south  = old_grid.row_data(i + 1);

        double* out = new_grid.row_data(i);

        // Copy left boundary
        out[0] = center[0];

        // Compute interior cells
        #pragma omp simd
        for (std::size_t j = 1; j < cols - 1; ++j) {
            out[j] =
                0.5 * center[j]
                + 0.125 * (
                    north[j] +
                    south[j] +
                    center[j - 1] +
                    center[j + 1]
                );
        }

        // Copy right boundary
        out[cols - 1] = center[cols - 1];
    }
}