#pragma once

#include <cstddef>
#include <vector>

struct GridViewConst {
    const double* __restrict__ data; //using __restrict__ hint to remove pointer aliasing
    std::size_t rows;
    std::size_t cols;

    double operator()(std::size_t r, std::size_t c) const {
        return data[r * cols + c];
    }
};

struct GridView {
    double* __restrict__ data;
    std::size_t rows;
    std::size_t cols;

    double& operator()(std::size_t r, std::size_t c) const {
        return data[r * cols + c];
    }
};

class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;

  std::vector <double> g = {}; //1D Vector over 2D for contiguous memory allocation to fully capitalize on future SIMD implementations

public:
  Grid(std::size_t rows, std::size_t cols);

  double& operator()(std::size_t i, std::size_t j);
  double  operator()(std::size_t i, std::size_t j) const;

  std::size_t getrows() const;
  std::size_t getcols() const;

  GridViewConst const_view() const {
      return GridViewConst{g.data(), rows_, cols_};
  }

  GridView view() {
      return GridView{g.data(), rows_, cols_};
  }
};  
void apply_stencil(const Grid& old_grid, Grid& new_grid);


Grid::Grid(const std::size_t rows, const std::size_t cols) {
    rows_ = rows;
    cols_ = cols;

    g.resize(rows * cols, 0.0);
}

double& Grid::operator()(std::size_t i, std::size_t j) {
    return g[(i*cols_)+j];
}

double  Grid::operator()(std::size_t i, std::size_t j) const {
    return g[(i*cols_)+j];
}

std::size_t Grid::getrows() const {
    return rows_;
}

std::size_t Grid::getcols() const {
    return cols_;
}


void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const GridViewConst in = old_grid.const_view(); //using less expensive view pointers
  const GridView out = new_grid.view();
  
  const std::size_t row = (in.rows - 1); 
  const std::size_t col = (in.cols - 1);

  for (std::size_t x = 0; x <= col; x++) {
    out(0, x) = in(0, x);
    out(row, x) = in(row, x);
  }

  for (std::size_t y = 1; y < row; y++) {
    out(y, 0) = in(y, 0);
    out(y, col) = in(y, col); 
  }

  for (std::size_t y = 1; y < row; y++) {
      #pragma omp simd //Forces compiler to vectorize inner loop
      for (std::size_t x = 1; x < col; x++) {
        out(y, x) = (0.5 * in(y, x)) + (0.125 * in((y-1), x)) + (0.125 * in((y), (x-1))) + (0.125 * in((y), (x+1))) + (0.125 * in((y+1), x));
      }
    }
}