#pragma once

#include <cstddef>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <vector>


class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::size_t elem_stride_;
  double* data_;

public:
  explicit Grid(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols){
    size_t byte_stride = (cols * sizeof(double) + 63)& ~ 63;
    elem_stride_ = byte_stride / sizeof(double);

    data_ = new double[rows * elem_stride_];
    for(size_t i = 0, i < rows_ * elem_stride_, i++){
      data_[i] = 0.0;
    }
  ~ Grid{
    delete[] data_;}

  }
  Grid(const Grid&) = delete;
  Grid &operator = (const Grid&) = delete;

  double* raw() noexcept {return data_;}

  double& operator()(std::size_t i, std::size_t j){
    if(i >= rows_ || j >= cols_){
      throw std::out_of_range(" Grid index out of bound ");
    }
    return data_[i * elem_stride_ + j];
  }
  double operator()(std::size_t i, std::size_t j) const{
    if(i >= rows_ || j >=cols_){
      throw std::out_of_range(" Grid index out of bound");
    }
    return data_[i * elem_stride_ + j];
  }
  size_t rows() const noexcept { return rows_; }
  size_t cols() const noexcept { return cols_; }
  const size_t &stride() const noexcept { return elem_stride_;}

};  

void apply_stencil(const Grid& old_grid, Grid& new_grid){
  const double *old_data = old_grid.raw();
  double *new_data = new_grid.raw();
  size_t r = old_grid.rows();
  size_t c = old_grid.cols();
  size_t s_old = old_grid.stride();
  size_t s_new = new_grid.stride();

  for(size_t i = 0, i < r, ++i){
    new_data[i * s_new] = old_data[i * s_old];
    if (r > 1)
    new_data[i * s_new + c - 1] = old_data[i * s_old + c - 1];
  }
  }
  if (c > 1){
    std::memcpy(new_data, old_data, c * sizeof(double));
    std::memcpy(new_data + (r - 1) * s_new, old_data + (r - 1) * s_old, c * sizeof(double));
  }


#pragma omp parallel for schedule(static)
for (size_t i = 1, i < r - 1, i++){
  const double* L_row = old_data + (i - 1) * s_old;
  const double* C_row = old_data + i * s_old;
  const double* U_row = old_data + (i + 1) * s_old;
  double* new_data_row = new_data +  i * s_new;
}
#pragma omp simd aligned(L_row, C_row, U_row, new_data_row : 64) safelen(8)
for (size_t j  = 1,  j < c - 1, j++){
  new_data_row[j] = 0.5 * C_row[j] + 0.125 * (L_row[j]  + U_row[j] + C_row[j-1] + C_row[j+1]);
}
