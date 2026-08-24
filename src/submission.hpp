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
    const double* data() const {return data_.data();}
    double* data() {return data_.data();}

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

inline double five_point_stencil(const double* __restrict__ src, std::size_t offset, std::size_t cols) {
    return 0.5 * src[offset] + 0.125 * (src[offset - cols] + src[offset + cols] +
                                           src[offset - 1] + src[offset + 1]);
}

void apply_stencil(const Grid &old_grid, Grid &new_grid) {
    Dimensions dimensions{old_grid.get_dimensions()};

    const double* __restrict__ src = old_grid.data();
    double* __restrict__ dest = new_grid.data();

    for (std::size_t j = 0; j < dimensions.cols; j++) {
        dest[0 * dimensions.cols + j] = src[0 * dimensions.cols + j];
        dest[(dimensions.rows - 1) * dimensions.cols + j] = src[(dimensions.rows - 1) * dimensions.cols + j];
    }

    for (std::size_t i = 0; i < dimensions.rows; i++) {
        dest[i * dimensions.cols + 0] = src[i * dimensions.cols + 0];
        dest[i * dimensions.cols + (dimensions.cols - 1)] = src[i * dimensions.cols + (dimensions.cols - 1)];
    }

#pragma omp parallel for
    for (std::size_t i = 1; i < dimensions.rows - 1; i++) {
        const std::size_t location = i * dimensions.cols;

#pragma omp simd
        for (std::size_t j = 1; j < dimensions.cols - 1; j++) {
            dest[i * dimensions.cols + j] = five_point_stencil(src, location + j, dimensions.cols);
        }
    }
}
