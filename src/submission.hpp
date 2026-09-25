#pragma once

#include <cstddef>
#include <cstring>
#include <vector>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid
{
private:
    const std::size_t rows_;
    const std::size_t cols_;
    std::vector<double> grid_vec_;

public:
    Grid(std::size_t rows, std::size_t cols)
        : rows_(rows), cols_(cols), grid_vec_(rows * cols, 0.0) {}

    double &operator()(std::size_t i, std::size_t j)
    {
        return grid_vec_[i * cols_ + j];
    }
    double operator()(std::size_t i, std::size_t j) const
    {
        return grid_vec_[i * cols_ + j];
    }
    double *data()
    {
        return grid_vec_.data();
    }

    const double *data() const
    {
        return grid_vec_.data();
    }

    std::size_t rows() const
    {
        return rows_;
    }
    std::size_t cols() const
    {
        return cols_;
    }
};

inline void fix_boundary(const double *old_grid_data, double *new_grid_data,
                         std::size_t rows, std::size_t cols)
{
    if (rows == 0 || cols == 0)
        return;

    std::memcpy(new_grid_data, old_grid_data, cols * sizeof(double));
    if (rows > 1)
        std::memcpy(new_grid_data + (rows - 1) * cols,
                    old_grid_data + (rows - 1) * cols, cols * sizeof(double));

    for (std::size_t i = 1; i + 1 < rows; ++i)
    {
        new_grid_data[i * cols] = old_grid_data[i * cols];
        new_grid_data[i * cols + cols - 1] = old_grid_data[i * cols + cols - 1];
    }
}

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
inline void apply_stencil(const Grid &old_grid, Grid &new_grid)
{
    const std::size_t rows = old_grid.rows();
    const std::size_t cols = old_grid.cols();

    const double *__restrict__ old_grid_data = old_grid.data();
    double *__restrict__ new_grid_data = new_grid.data();

    fix_boundary(old_grid_data, new_grid_data, rows, cols);
    if (rows < 3 || cols < 3)
        return;

#pragma omp parallel for schedule(static)
    for (std::size_t i = 1; i < rows - 1; ++i)
    {
        const std::size_t row_start = i * cols;
#pragma omp simd
        for (std::size_t j = 1; j < cols - 1; ++j)
        {
            const std::size_t center = row_start + j;
            new_grid_data[center] = 0.5 * old_grid_data[center] +
                                    0.125 * (old_grid_data[center - cols] + old_grid_data[center - 1] +
                                             old_grid_data[center + 1] + old_grid_data[center + cols]);
        }
    }
}
