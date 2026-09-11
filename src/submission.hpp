#pragma once

#include <cstddef>
#include <vector>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
    std::size_t rows_;
    std::size_t cols_;
    std::size_t stride_; // break cache-set aliasing
    std::vector<double> grid_; // 1D for contiguous storage

public:
    Grid(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols), stride_(rows + 1), grid_(stride_ * cols, 0.0) {};

    double& operator()(std::size_t i, std::size_t j) {
        return grid_[j * stride_ + i];
    };
    double operator()(std::size_t i, std::size_t j) const {
        return grid_[j * stride_ + i];
    };

    // getters for the data
    std::size_t rows() const { return rows_; }
    std::size_t cols() const { return cols_; }
    std::size_t stride() const { return stride_; }
    double* data() { return grid_.data(); }
    const double* data() const { return grid_.data(); }
};  

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {
    const std::size_t rows = old_grid.rows(); // column size
    const std::size_t cols = old_grid.cols(); // row size
    const std::size_t stride = old_grid.stride();

    const double* __restrict old_ptr = old_grid.data();
    double* __restrict new_ptr = new_grid.data();

    // copy edge columns
    for (std::size_t i = 0; i < rows; i++) {
        new_ptr[i] = old_ptr[i];
        new_ptr[(cols - 1) * stride + i] = old_ptr[(cols - 1) * stride + i];
    }

    // traverse in fortran order (no boundary conditions)

    #pragma omp parallel for // break iterations into parallel threads
    for (std::size_t j = 1; j < cols - 1; j++) {
        const double* __restrict in = old_ptr + j * stride;
        const double* __restrict l = in - stride;
        const double* __restrict r = in + stride;
        
        double* __restrict out = new_ptr + j * stride;
        
        for (std::size_t i = 1; i < rows - 1; i++) {
            out[i] = 0.5 * in[i] + 0.125 * (l[i] + r[i] + in[i - 1] + in[i + 1]);
        }
        
        out[0] = in[0];
        out[rows - 1] = in[rows - 1];
    }
};
