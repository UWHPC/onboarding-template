#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

// Groups row and column counts
struct GridShape {
    std::size_t rows;
    std::size_t cols;
};

// Groups shape with row stride
struct GridLayout {
    // Shape is the useable rows and columns, not the storage size
    GridShape shape;
    // Stride is distance between one row to the next, cols is usuable columns
    std::size_t stride;
};

// A view borrows storage, so copying it copies only a pointer and layout.
// The owning grid must not be destroyed, and do not replace or move its storage
template <typename T>
struct BasicGridView {
    T* data;
    GridLayout layout;

    T* row(std::size_t i) const {
        return data + i * layout.stride;
    }
};

// Provide read/write access to existing storage using the templates
using GridView = BasicGridView<double>;
using ConstGridView = BasicGridView<const double>;


class Grid {
private:
    GridLayout layout_;
    std::vector<double> data_;

    // Compute the number of elements needed to store a grid with the given layout.
    static std::size_t storage_size(GridLayout layout) {
        if (layout.stride != 0 && layout.shape.rows > std::numeric_limits<std::size_t>::max() / layout.stride) {
            throw std::length_error("Grid dimensions overflow");
        }
        return layout.shape.rows * layout.stride;
    }

public:
    // This version has no padding, so stride equals the number of columns.
    Grid(std::size_t rows, std::size_t cols)
        : layout_{{rows, cols}, cols}, data_(storage_size(layout_), 0.0) {}

    double& operator()(std::size_t i, std::size_t j) {
        return data_[i * layout_.stride + j];
    }

    double operator()(std::size_t i, std::size_t j) const {
        return data_[i * layout_.stride + j];
    }
    
    std::size_t rows() const {
        return layout_.shape.rows; 
    }
    std::size_t cols() const { 
        return layout_.shape.cols; 
    }

    GridView view() & { 
        return {
            data_.data(), layout_
        }; 
    }
    ConstGridView view() const & { 
        return {
            data_.data(), layout_
        }; 
    }

    // A view of a temporary grid would immediately become dangling so delete
    GridView view() && = delete;
    ConstGridView view() const && = delete;

};

namespace stencil_detail {

    // inline for everything so call overhead can be optimized

    inline void validate_grids(const Grid& input, const Grid& output) {
        if (&input == &output) {
            throw std::invalid_argument("The stencil needs two distinct grids");
        }
        if (input.rows() != output.rows() || input.cols() != output.cols()) {
            throw std::invalid_argument("Grid dimensions must match");
        }
    }

    // Copy the boundary rows and columns from input to output. The interior is not modified.
    inline void copy_boundaries(ConstGridView input, GridView output) {
        const auto shape = input.layout.shape;
        if (shape.rows == 0 || shape.cols == 0) {
            return;
        }

        std::copy_n(input.row(0), shape.cols, output.row(0));
        if (shape.rows > 1) {
            std::copy_n(input.row(shape.rows - 1), shape.cols, output.row(shape.rows - 1));
        }
    }

    // Update a single interior row of the output grid using the stencil formula.
    inline void update_row(ConstGridView input, GridView output, std::size_t i) {
        // The row index is guaranteed to be in the interior, so the neighbors exist.
        const double* above = input.row(i - 1); 
        const double* curr = input.row(i);
        const double* below = input.row(i + 1); 
        double* out = output.row(i);
        const std::size_t cols = input.layout.shape.cols;

        // Update left boundary
        out[0] = curr[0];

        // Each iteration writes a different cell, all reads use separate storage.
    #ifdef _OPENMP
        #pragma omp simd
    #endif
        for (std::size_t j = 1; j < cols - 1; ++j) {
            out[j] = 0.5 * curr[j]
                + 0.125 * (above[j] + below[j] + curr[j - 1] + curr[j + 1]);
        }
        
        // Update right boundary
        if (cols > 1) out[cols - 1] = curr[cols - 1];
    }

    // Update all interior rows of the output grid using the stencil formula.
    inline void update_interior(ConstGridView input, GridView output) {
        const auto shape = input.layout.shape;

        // If the grid is too small to have an interior, do nothing
        // Run update interior even if not enough cols for interior except 0, has to update boundaries still
        if (shape.rows < 3 || shape.cols == 0) {
            return;
        }

        // Random cutoff 2^16; OpenMP chooses the worker count per machine.
    #ifdef _OPENMP
        #pragma omp parallel for schedule(static) if(shape.rows * shape.cols >= 65536)
    #endif
        for (std::size_t i = 1; i < shape.rows - 1; ++i) {
            update_row(input, output, i);
        }
    }

}


// Apply stencil reads like exactly what is going to happen, changed to multiple functions
inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
    stencil_detail::validate_grids(old_grid, new_grid);
    const ConstGridView input = old_grid.view();
    const GridView output = new_grid.view();
    stencil_detail::copy_boundaries(input, output);
    stencil_detail::update_interior(input, output);
}
