#pragma once

#include <cstddef>
#include <vector>

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

void apply_stencil(const Grid &old_grid, Grid &new_grid) {
    Dimensions dimensions{old_grid.get_dimensions()};

    for (std::size_t j = 0; j < dimensions.cols; j++) {
        new_grid(0, j) = old_grid(0, j);
        new_grid(dimensions.rows - 1, j) = old_grid(dimensions.rows - 1, j);
    }

    for (std::size_t i = 0; i < dimensions.rows; i++) {
        new_grid(i, 0) = old_grid(i, 0);
        new_grid(i, dimensions.cols - 1) = old_grid(i, dimensions.cols - 1);
    }

#pragma omp parallel for
    for (std::size_t i = 1; i < dimensions.rows - 1; i++) {
#pragma omp simd
        for (std::size_t j = 1; j < dimensions.cols - 1; j++) {
            new_grid(i, j) = five_point_stencil(old_grid, i, j);
        }
    }
}
