#pragma once

#include <cstddef>
#include <memory>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::unique_ptr<double[]> data_;

public:
  Grid(std::size_t rows, std::size_t cols) :
        rows_(rows),
        cols_(cols),
        data_(std::make_unique<double[]>(rows * cols)) {}
  
  std::size_t num_rows() const {
    return rows_;
  }
  std::size_t num_cols() const {
    return cols_;
  }

  const double* aPointer() const {
    return data_.get();
  }
  double* wPointer() const {
    return data_.get();
  }

  double& operator()(std::size_t row, std::size_t col) {
    return data_[row * cols_ + col];
  }

  double operator()(std::size_t row, std::size_t col) const {
    return data_[row * cols_ + col];
  }
};  

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {

  const std::size_t rows = old_grid.num_rows();
  const std::size_t cols = old_grid.num_cols();

  std::size_t iRows = rows - 1;
  std::size_t iCols = cols - 1;
  
  const double* oldP = old_grid.aPointer();
  double* newP = new_grid.wPointer();

  for (std::size_t k = 0; k < cols; k++) {
    newP[k] = oldP[k];
    newP[iRows*cols+k] = oldP[iRows*cols+k];
  }

  for (std::size_t k = 1; k < iRows; k++) {
    newP[k*cols] = oldP[k*cols];
    newP[k*cols+iCols] = oldP[k*cols+iCols];
  }

  #pragma omp parallel for
  for (std::size_t i = 1; i < iRows; i++) {
    for (std::size_t j = 1; j < iCols; j++) {
      newP[i*cols+j] = 0.125 * (4 * oldP[i*cols+j] + oldP[(i-1)*cols+j] + oldP[i*cols+j-1] + oldP[i*cols+j+1] + oldP[(i+1)*cols+j]);
    }
  }

}
