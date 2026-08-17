#pragma once
#include <cstddef>
#include <vector>

class Grid {
  std::size_t rows_, cols_;
  std::vector<double> c_;
public:
  Grid(std::size_t r, std::size_t co) : rows_{r}, cols_{co}, c_(r * co, 0.0) {}
  double& operator()(std::size_t i, std::size_t j)       { return c_[i * cols_ + j]; }
  double  operator()(std::size_t i, std::size_t j) const { return c_[i * cols_ + j]; }
  std::size_t rows() const { return rows_; }
  std::size_t cols() const { return cols_; }
};

inline void apply_stencil(const Grid& o, Grid& n) {
  const std::size_t R = o.rows(), C = o.cols();
  for (std::size_t i = 0; i < R; ++i) { n(i, 0) = o(i, 0); n(i, C - 1) = o(i, C - 1); }
  for (std::size_t j = 0; j < C; ++j) { n(0, j) = o(0, j); n(R - 1, j) = o(R - 1, j); }
  for (std::size_t i = 1; i < R - 1; ++i)
    for (std::size_t j = 1; j < C - 1; ++j)
      n(i, j) = 0.5 * o(i, j) + 0.125 * (o(i-1, j) + o(i+1, j) + o(i, j-1) + o(i, j+1));
}
