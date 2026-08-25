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
  std::vector<double> data_;

public:
  Grid(std::size_t rows, std::size_t cols);

  double& operator()(std::size_t i, std::size_t j);
  double  operator()(std::size_t i, std::size_t j) const;
  std::size_t rows() const { return rows_; }
  std::size_t cols() const { return cols_; }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid);



Grid::Grid(std::size_t rows, std::size_t cols)
  : rows_{rows}
  , cols_{cols}
  , data_(rows * cols, 0.0)
{
}

double& Grid::operator()(std::size_t i, std::size_t j) {
  return data_[i * cols_ + j];
}

double Grid::operator()(std::size_t i, std::size_t j) const {
  return data_[i * cols_ + j];
}

void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const std::size_t rows{old_grid.rows()};
  const std::size_t cols{old_grid.cols()};

  for (std::size_t row{}; row < rows; ++row) {
    new_grid(row, 0) = old_grid(row, 0);
    new_grid(row, cols - 1) = old_grid(row, cols - 1);
  }

  for (std::size_t col{}; col < cols; ++col) {
    new_grid(0, col) = old_grid(0, col);
    new_grid(rows - 1, col) = old_grid(rows - 1, col);
  }
  if(rows < 3 || cols < 3) return;

  std::vector<std::thread> threads;
  const std::size_t num_threads{4};
  threads.reserve(num_threads);

  auto compute = [&](std::size_t start_row, std::size_t end_row) {for (std::size_t row{start_row}; row <= end_row; ++row) {
    for (std::size_t col{1}; col < cols - 1; ++col) {
      new_grid(row, col) = 0.5   * old_grid(row, col) +
                           0.125 * (old_grid(row - 1, col) + old_grid(row + 1, col) +
                                    old_grid(row, col - 1) + old_grid(row, col + 1));
    }
  }};
  const std::size_t rows_per_thread = (rows - 2) / num_threads;
  const std::size_t remaining_rows = (rows - 2) % num_threads;
  std::size_t start_row = 1;
  for (std::size_t i = 0; i < num_threads; ++i) {
    std::size_t end_row = start_row + rows_per_thread - 1;
    if (i < remaining_rows) {
      end_row += 1;
    }
    threads.emplace_back(compute, start_row, end_row);
    start_row = end_row + 1;
  }
  for (auto& thread : threads) {
    if(thread.joinable()) thread.join();
  }
}