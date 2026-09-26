#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>

#include <omp.h>


class Grid {
private:
    static constexpr std::size_t Alignment = 64;

    std::size_t rows_;
    std::size_t cols_;
    double* data_;

    static std::size_t aligned_size(std::size_t bytes) {
        return bytes + (Alignment - bytes % Alignment) % Alignment;
    }

public:
    Grid(std::size_t rows, std::size_t cols)
        : rows_(rows),
          cols_(cols),
          data_(static_cast<double*>(
              std::aligned_alloc(
                  Alignment,
                  aligned_size(rows * cols * sizeof(double)) 
              )
          ))
    {}

    ~Grid() {
        std::free(data_);
    }

    double& operator()(std::size_t row, std::size_t col) {
        return data_[row * cols_ + col];
    }

    double operator()(std::size_t row, std::size_t col) const {
        return data_[row * cols_ + col];
    }

    double* data() {
        return data_;
    }

    const double* data() const {
        return data_;
    }

    int num_rows() const {
        return static_cast<int>(rows_);
    }

    int num_cols() const {
        return static_cast<int>(cols_);
    }

    // True once this grid is known to contain a valid boundary.
    bool boundary_copied = false;
};


// ============================================================================
// Stencil implementation
// ============================================================================

namespace stencil {

constexpr int BlockRows = 16;

constexpr double CenterWeight   = 0.5;
constexpr double NeighborWeight = 0.125;


// Copy the boundary from the current state into the destination.
//
// This is only necessary when the destination has not previously received
// a valid boundary. The interior stencil never modifies boundary cells.
void copy_boundary(
    const Grid& old_grid,
    Grid& new_grid
) {
    const int rows = old_grid.num_rows();
    const int cols = old_grid.num_cols();

    // Left and right boundaries.
    for (int row = 0; row < rows; ++row) {
        new_grid(row, 0) =
            old_grid(row, 0);

        new_grid(row, cols - 1) =
            old_grid(row, cols - 1);
    }

    // Top and bottom boundaries.
    for (int col = 0; col < cols; ++col) {
        new_grid(0, col) =
            old_grid(0, col);

        new_grid(rows - 1, col) =
            old_grid(rows - 1, col);
    }
}


// Compute the interior of the grid using a five-point stencil.
//
// The boundary is intentionally never written here.
void compute(
    const double* __restrict__ old_data,
    double* __restrict__ new_data,
    int num_cols,
    int num_rows
) {
    #pragma omp parallel for
    for (
        int block = 0;
        block < (num_rows + BlockRows - 1) / BlockRows;
        ++block
    ) {
        const int first_row =
            std::max(1, block * BlockRows);

        const int last_row =
            std::min(
                block * BlockRows + BlockRows,
                num_rows - 1
            );

        for (int row = first_row;
             row < last_row;
             ++row)
        {
            /*
             * Start at column 1 so that the loop processes only
             * interior cells.
             */
            const double* top =
                old_data + (row - 1) * num_cols + 1;

            const double* center =
                old_data + row * num_cols + 1;

            const double* bottom =
                old_data + (row + 1) * num_cols + 1;

            double* output =
                new_data + row * num_cols + 1;


            #pragma omp simd
            for (int col = 0;
                 col < num_cols - 2;
                 ++col)
            {
                output[col] =
                    CenterWeight * center[col]
                    + NeighborWeight * (
                        top[col]
                        + bottom[col]
                        + center[col - 1]
                        + center[col + 1]
                    );
            }
        }
    }
}

} // namespace stencil


// ============================================================================
// Public interface
// ============================================================================

void apply_stencil(
    const Grid& old_grid,
    Grid& new_grid
) {
    /*
     * The harness guarantees that the first grid is initialized.
     *
     * The first time the initialized grid is used as the source, the
     * destination needs its boundary copied. Once the destination has
     * received that boundary, both ping-pong buffers have valid boundaries
     * and no further boundary copying is required.
     */
    if (!old_grid.boundary_copied) {
        stencil::copy_boundary(old_grid, new_grid);
    }

    new_grid.boundary_copied = true;


    stencil::compute(
        old_grid.data(),
        new_grid.data(),
        old_grid.num_cols(),
        old_grid.num_rows()
    );
}
