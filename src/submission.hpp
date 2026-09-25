#pragma once

#include <cstddef>
#include <vector>

// Grid for storing the temperature values used in the
// two-dimensional heat diffusion simulation.
class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;

  // Even though the grid is treated as two-dimensional,
  // the temperature values are stored in one continuous vector.
  std::vector<double> temperatures_;

  // Convert a row and column into the corresponding position
  // inside the one-dimensional temperature vector.
  std::size_t position(std::size_t i, std::size_t j) const {
    return (i * cols_) + j;
  }

public:
  // Construct a grid with the requested dimensions.
  // Every temperature begins at 0.0.
  Grid(std::size_t rows, std::size_t cols)
      : rows_(rows),
        cols_(cols),
        temperatures_(rows * cols, 0.0) {
  }

  // Allow a grid value to be changed.
  double& operator()(std::size_t i, std::size_t j) {
    return temperatures_[position(i, j)];
  }

  // Allow a grid value to be read when the Grid is const.
  double operator()(std::size_t i, std::size_t j) const {
    return temperatures_[position(i, j)];
  }

  // The stencil needs to know the dimensions of the grid,
  // so expose them without allowing them to be changed.
  std::size_t rows() const {
    return rows_;
  }

  std::size_t cols() const {
    return cols_;
  }
};


// Apply one step of the five-point heat diffusion stencil.
//
// Boundary cells are copied directly from old_grid.
// Only cells strictly inside the boundary are recalculated.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {

  const std::size_t number_of_rows = old_grid.rows();
  const std::size_t number_of_cols = old_grid.cols();

  // There is nothing to update if the grid has no rows or columns.
  if (number_of_rows == 0 || number_of_cols == 0) {
    return;
  }

  // ------------------------------------------------------------
  // Part 1: Copy the top and bottom edges.
  // ------------------------------------------------------------

  for (std::size_t j = 0; j < number_of_cols; ++j) {
    new_grid(0, j) = old_grid(0, j);

    // If there is only one row, the top and bottom are
    // actually the same row, so do not copy it twice.
    if (number_of_rows > 1) {
      new_grid(number_of_rows - 1, j) =
          old_grid(number_of_rows - 1, j);
    }
  }


  // ------------------------------------------------------------
  // Part 2: Copy the left and right edges.
  // ------------------------------------------------------------

  // Start at row 1 because the corners were already copied
  // when the top and bottom rows were handled.
  for (std::size_t i = 1; i + 1 < number_of_rows; ++i) {
    new_grid(i, 0) = old_grid(i, 0);

    // If there is only one column, the left and right edges
    // are the same column.
    if (number_of_cols > 1) {
      new_grid(i, number_of_cols - 1) =
          old_grid(i, number_of_cols - 1);
    }
  }


  // ------------------------------------------------------------
  // Part 3: Recalculate every interior temperature.
  // ------------------------------------------------------------

  for (std::size_t i = 1; i + 1 < number_of_rows; ++i) {

    for (std::size_t j = 1; j + 1 < number_of_cols; ++j) {

      const double center = old_grid(i, j);

      const double above = old_grid(i - 1, j);
      const double below = old_grid(i + 1, j);
      const double left  = old_grid(i, j - 1);
      const double right = old_grid(i, j + 1);

      const double neighbor_sum =
          above + below + left + right;

      new_grid(i, j) =
          (0.5 * center) +
          (0.125 * neighbor_sum);
    }
  }
}