#pragma once

#include <cassert>
#include <cstddef>
#include <vector>

class Grid {
private:
    std::size_t rows_;
    std::size_t cols_;
    std::vector<double> data_;

public:
    // construct a grid with all temperatures initialized to zero
    Grid(std::size_t rows, std::size_t cols)
        : rows_(rows),
          cols_(cols),
          data_(rows * cols, 0.0)
    {}

    // read/write access to a grid cell
    double& operator()(std::size_t i, std::size_t j) {
        return data_[i * cols_ + j];
    }

    // read-only access to a grid cell
    double operator()(std::size_t i, std::size_t j) const {
        return data_[i * cols_ + j];
    }

    std::size_t rows() const {
        return rows_;
    }

    std::size_t cols() const {
        return cols_;
    }
};

// advance the temperature field by one time step; input and output must be distinct grids of matching dimensions
inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
    assert(old_grid.rows() == new_grid.rows());
    assert(old_grid.cols() == new_grid.cols());
    assert(&old_grid != &new_grid);

    const std::size_t rows = old_grid.rows();
    const std::size_t cols = old_grid.cols();

    for (std::size_t i = 0; i < rows; ++i) {
        for (std::size_t j = 0; j < cols; ++j) {

            // copy boundary temperatures unchanged
            if (i == 0 || j == 0 ||
                i == rows - 1 || j == cols - 1) {

                new_grid(i, j) = old_grid(i, j);
            }
            else {
                // five-point heat diffusion stencil
                new_grid(i, j) =
                    0.5 * old_grid(i, j)
                    + 0.125 * (
                        old_grid(i - 1, j)
                        + old_grid(i + 1, j)
                        + old_grid(i, j - 1)
                        + old_grid(i, j + 1)
                    );
            }
        }
    }
}