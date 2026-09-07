#pragma once

#include <cstddef>
#include <thread>
#include <vector>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> arr;

public:
  Grid(std::size_t rows, std::size_t cols)
  : rows_(rows), cols_(cols), arr(rows*cols){}

  double& operator()(std::size_t i, std::size_t j){
    return arr[i * cols_ + j];
  }

  double  operator()(std::size_t i, std::size_t j) const{
    return arr[i * cols_ + j];
  }

  double get(std::size_t i) const{
    return arr[i];
  }

  std::size_t getRows() const{
    return rows_;
  }

  std::size_t getCols() const{
    return cols_;
  }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid){
  std::size_t r = old_grid.getRows()-1;
  std::size_t c = old_grid.getCols()-1;
  std::size_t d = old_grid.getCols();

  #pragma omp parallel for
  for (std::size_t i = 1; i < r;i++)
    {
      std::size_t k = i * (c + 1);
      for (std::size_t j = 1; j < c; j++)
      {
        new_grid(i, j) = old_grid.get(k+j) * 0.5 + 0.125 * (old_grid.get(k+j-1) + old_grid.get(k+j+1) + old_grid.get((i+1)*d+j) + old_grid.get((i-1)*d+j));
      }
    }

  std::size_t k = r * (c + 1);
  for (std::size_t j = 0; j <= c;j++)
  {
    new_grid(0, j) = old_grid.get(j);
    new_grid(r, j) = old_grid.get(k+j);
  }

  for (std::size_t i = 1; i <= r; i++)
  {
    std::size_t m = i*(c+1);
    new_grid(i, 0) = old_grid.get(m);
    new_grid(i, c) = old_grid.get(m+c);
  }
}
