#pragma once

#include <cstddef>
#include <vector>
#pragma GCC optimize("Ofast")

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
  const std::size_t maxrow_;
  const std::size_t maxcol_;
  const std::size_t maxindex_;

public:
  Grid(std::size_t rows, std::size_t cols);

  std::vector<double> array;

  double &operator()(std::size_t i, std::size_t j);
  double operator()(std::size_t i, std::size_t j) const;

  std::size_t rows() const {
    return rows_;
  }
  std::size_t cols() const {
    return cols_;
  }
  std::size_t maxrow() const {
    return maxrow_;
  }
  std::size_t maxcol() const {
    return maxcol_;
  }
  std::size_t maxindex() const {
    return maxindex_;
  }


};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid &old_grid, Grid &new_grid);

Grid::Grid(std::size_t rows, std::size_t cols)
:  rows_(rows), 
  cols_(cols), 
  maxrow_(rows-1), 
  maxcol_(cols-1), 
  maxindex_(rows*cols-1),
  array(rows*cols, 0)
  {  }

inline double& Grid::operator()(std::size_t i, std::size_t j) {
  return this->array[i*cols_+j];
}

inline double Grid::operator()(std::size_t i, std::size_t j) const {
  return this->array[i*cols_+j];
}

inline void apply_stencil(const Grid &old_grid, Grid &new_grid) {

  #pragma omp parallel for
  for (std::size_t i = 1; i < old_grid.maxrow(); i++) {
    
    std::size_t i2 = i*old_grid.cols();

    //Set the first and last cell of each row
    new_grid.array[i2] = old_grid.array[i2];
    new_grid.array[old_grid.cols()-1+i2] = old_grid.array[old_grid.cols()-1+i2];

    for (std::size_t j = 1; j < old_grid.maxcol(); j++) {
      
      new_grid.array[i2+j] = 0.5*(old_grid.array[i2+j]) +0.125*(old_grid.array[i2+j-1] + old_grid.array[i2+j+1] + old_grid.array[i2+j+old_grid.cols()] + old_grid.array[i2+j-old_grid.cols()]);
    }


  }

  //Top and bottom row
  for (std::size_t i = 0; i < old_grid.cols(); i++) {
    new_grid.array[i] = old_grid.array[i];
    new_grid.array[old_grid.maxindex()-i] = old_grid.array[old_grid.maxindex()-i];
  }
}