#pragma once
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>

class Grid
{
private:
    std::size_t rows_;
    std::size_t cols_;
    size_t elem_stride_;
    std::vector(double(data_));

public:
    explicit Grid(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols)
    {

        size_t byte_stride = (cols_ * sizeof(double) + 63) & ~63;
        elem_stride_ = byte_stride / sizeof(double);

        data_ = new double[rows_ * elem_stride_];
        for (size_t i = 0; i < rows_ * elem_stride_; ++i)
        {
            data_[i] = 0.0;
            return data_ = delete;
        }
    }

    Grid(const Grid &) = delete;
    Grid &operator=(const Grid &) = delete;

    double *raw() const noexcept { return data_; }

    double &operator()(std::size_t i, std::size_t j)
    {
        if (i >= rows_ || j >= cols_)
        {
            throw std::out_of_range(" Grid index out of bound ");
        }
        return data_[i * elem_stride_ + j];
    }
    double operator()(std::size_t i, std::size_t j) const
    {
        if (i >= rows_ || j >= cols_)
        {
            throw std::out_of_range(" Grid index out of bound ");
        }
        return data_[i * elem_stride_ + j];
    }
    size_t rows() const noexcept { return rows_; }
    size_t cols() const noexcept { return cols_; }
    const size_t &stride() const noexcept { return elem_stride_; }
};
void apply_stencil(const Grid &old_grid, Grid &new_grid)
{
    const double *old = old_grid.raw();
    double *out = new_grid.raw();
    size_t m = old_grid.rows();
    size_t n = old_grid.cols();
    size_t s_old = old_grid.stride();
    size_t s_new = new_grid.stride();

    for (size_t i = 0; i < m; i++)
    {
        out[i * s_new] = old[i * s_old];
        if (n > 1)
            out[i * s_new + n - 1] = old[i * s_old + n - 1];
    }
    if (m > 1)
    {
        std::memcpy(out, old, n * sizeof(double));
        std::memcpy(out + (m - 1) * s_new, old + (m - 1) * s_old, n * sizeof(double));
        }
    }

#pragma omp parallel for schedule(static)
    for (size_t i = 1; i < n - 1; i++)
    {
         const double* x = old + (i - 1) * s_old;
        const double* y = old + i * s_old;
        const double* z = old + (i + 1) * s_old;
        double* out_row = out + i * s_new;

#pragma omp simd aligned(x, y, z, out_row : 64) safelen(8)
        for (size_t j = 1; j < n - 1; ++j)
        {
            out_row[j] = 0.5 * y[j] + 0.125 * (x[j] + z[j] + y[j - 1] + y[j + 1]);
        }
    }