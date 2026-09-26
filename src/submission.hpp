#pragma once

#include <cstddef>
#include <vector>
#include <cstring>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
    size_t rows_;
    size_t cols_;
    std::vector<double> data_;

public:
    Grid(std::size_t rows, size_t cols) : rows_(rows), cols_(cols), data_(rows * cols, 0.0) {}

    double& operator()(size_t i, size_t j) { return data_[i * cols_ + j]; }
    double  operator()(size_t i, size_t j) const { return data_[i * cols_ + j]; }

    size_t rows() const { return rows_; }
    size_t cols() const { return cols_; }

    double* data() { return data_.data(); }
    const double* data() const { return data_.data(); }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
    const size_t rows = old_grid.rows();
    const size_t cols = old_grid.cols();

    std::memcpy(new_grid.data(), 
        old_grid.data(), 
        cols * sizeof(double));

    std::memcpy(new_grid.data() + (rows-1) * cols, 
        old_grid.data() + (rows-1) * cols, 
        cols * sizeof(double));
    
    #pragma omp parallel for
    for (size_t i = 1; i < rows-1; i++) {
        size_t base = i*cols;
        const double* old_row = old_grid.data() + base;
        const double* old_prev_row = old_grid.data() + (base - cols);
        const double* old_next_row = old_grid.data() + (base + cols);
        double* new_row = new_grid.data() + base;
        
        new_row[0] = old_row[0];
        new_row[cols-1] = old_row[cols-1];

        #pragma omp simd
        for (size_t j = 1; j < cols-1; j++) {
            new_row[j] = 0.5 * old_row[j] + 0.125 * (old_row[j-1] + old_row[j+1] + old_prev_row[j] + old_next_row[j]);
        }
    }
}
