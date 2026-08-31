#pragma once

#include <cstddef>
#include <thread>
#include <vector>
// #include <iostream>

#define maxDistance 1000000

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::size_t step_;
  std::vector<double> data_;
  std::vector<std::size_t> distances_;
  std::size_t space_;

public:
  Grid(std::size_t rows, std::size_t cols);
  Grid(const Grid& other);
  Grid(Grid&& other) noexcept;

  double& operator()(std::size_t i, std::size_t j);
  double  operator()(std::size_t i, std::size_t j) const;
  double &operator()(std::size_t pos) { return data_[pos]; };
  double  operator()(std::size_t pos) const { return data_[pos]; };
  std::size_t rows() const { return rows_; }
  std::size_t cols() const { return cols_; }
  std::size_t space() const { return space_; }
  bool is_compute_needed(std::size_t i, std::size_t j) const {
    return distances_[i * cols_ + j] <= step_;
  }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid);



Grid::Grid(std::size_t rows, std::size_t cols)
  : rows_{rows}
  , cols_{cols}
  , step_{1}
  , data_(rows * cols, 0.0)
  , space_{(rows - 2) * (cols - 2)}
  // , distances_(rows * cols, 0)
{
}

Grid::Grid(const Grid& other)
  : rows_{other.rows_}
  , cols_{other.cols_}
  , step_{other.step_}
  , data_{other.data_}
  , distances_{other.distances_}
  , space_{other.space_}
{
  // std::cout << "Grid copy constructor called" << std::endl;
  if(other.step_ != 1) return;
  // std::cout << "Grid copy constructor called with step 1, computing distances" << std::endl;
  // std::this_thread::sleep_for(std::chrono::seconds(2));
  std::size_t rows{other.rows_ - 1};
  std::size_t cols{other.cols_ - 1};
  for(std::size_t i{1}; i < rows; ++i) {
    for(std::size_t j{1}; j < cols; ++j) {
      if(distances_[i * cols_ + j] != 0) continue;
      std::size_t up{j};
      std::size_t down{j};
      std::size_t left{i};
      std::size_t right{i};
      while(--up > 0 && other(i, up) == 0)
        ;
      while (++down < cols && other(i, down) == 0)
        ;
      while (--left > 0 && other(left, j) == 0)
        ;
      while(++right < rows && other(right, j) == 0)
        ;
      std::size_t dis_up{up ? j - up : maxDistance};
      std::size_t dis_down{down < cols ? down - j : maxDistance};
      std::size_t dis_left{left ? i - left : maxDistance};
      std::size_t dis_right{right < rows ? right - i : maxDistance};
      distances_[i * cols_ + j] = std::min(dis_down, std::min(dis_up, std::min(dis_left, dis_right)));
    }
  }
}

Grid::Grid(Grid&& other) noexcept
  : rows_{other.rows_}
  , cols_{other.cols_}
  , step_{other.step_ + 1}
  , data_{std::move(other.data_)}
  , distances_{std::move(other.distances_)}
  , space_{std::move(other.space_)}
{
  // std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // std::cout << "Grid move constructor called, step incremented to " << step_ << std::endl;
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

  // for (std::size_t row{}; row < rows; ++row) {
  //   new_grid(row, 0) = 0;
  //   new_grid(row, cols - 1) = 0;
  // }

  // for (std::size_t col{}; col < cols; ++col) {
  //   new_grid(0, col) = 0;
  //   new_grid(rows - 1, col) = 0;
  // }

  if(rows < 3 || cols < 3) return;

  std::vector<std::thread> threads;
  const std::size_t num_threads{4};
  threads.reserve(num_threads);

  auto compute = [&](std::size_t start_pos, std::size_t end_pos) {
    for (std::size_t pos{start_pos}; pos <= end_pos; ++pos) {
      std::size_t real_pos = pos + cols + 2 * (pos / (cols - 2)) + 1;
      new_grid(real_pos) = 0.5   * old_grid(real_pos) +
                          0.125 * (old_grid(real_pos - cols) + old_grid(real_pos + cols) +
                                  old_grid(real_pos - 1) + old_grid(real_pos + 1)); // equals to 0.125 * (left + center + right + center + up + center + down + center)
    }
  };
  const std::size_t cells_per_thread = old_grid.space() / (num_threads - 1);
  const std::size_t remaining_cells = old_grid.space() % (num_threads - 1);
  std::size_t start_pos{};
  for (std::size_t i{1}; i < num_threads; ++i) {
    std::size_t end_pos = start_pos + cells_per_thread - 1;
    if (i < remaining_cells) {
      end_pos += 1;
    }
    threads.emplace_back(compute, start_pos, end_pos);
    start_pos = end_pos + 1;
  }
  threads.emplace_back(compute, start_pos, old_grid.space() - 1);
  for (auto& thread : threads) {
    if(thread.joinable()) thread.join();
  }
}