#pragma once

#include <cstddef>
#include <vector>
#include <omp.h>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.

struct Dimensions {
    std::size_t rows;
    std::size_t cols;
};

class Grid {
private:
    std::size_t rows_;
    std::size_t cols_;
    std::vector<double> data_;

public:
    Grid(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols), data_(rows * cols, 0.0) {
    }

    double &operator()(std::size_t i, std::size_t j) {
        return data_[(i * cols_) + j];
    }

    Dimensions get_dimensions() const {
        return Dimensions{rows_, cols_};
    }

    double operator()(std::size_t i, std::size_t j) const {
        return data_[(i * cols_) + j];
    }
};

inline double five_point_stencil(const Grid &old_grid, std::size_t i, std::size_t j) {
    return 0.5 * old_grid(i, j) + 0.125 * (old_grid(i - 1, j) + old_grid(i + 1, j) +
                                           old_grid(i, j - 1) + old_grid(i, j + 1));
}

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid &old_grid, Grid &new_grid) {
    Dimensions dimensions{old_grid.get_dimensions()};

    const std::size_t rows{dimensions.rows};
    const std::size_t cols{dimensions.cols};

    if (rows < 2 || cols < 2) {
        return;
    }

    for (std::size_t j = 0; j < cols; j++) {
        new_grid(0, j) = old_grid(0, j);
    }

    for (std::size_t j = 0; j < cols; j++) {
        new_grid(rows - 1, j) = old_grid(rows - 1, j);
    }

    for (std::size_t i = 0; i < rows; i++) {
        new_grid(i, 0) = old_grid(i, 0);
    }

    for (std::size_t i = 0; i < rows; i++) {
        new_grid(i, cols - 1) = old_grid(i, cols - 1);
    }

#pragma omp parallel for
    for (std::size_t i = 1; i < rows - 1; i++) {
        for (std::size_t j = 1; j < cols - 1; j++) {
            new_grid(i, j) = five_point_stencil(old_grid, i, j);
        }
    }
}
