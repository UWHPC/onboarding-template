#pragma once

#pragma GCC optimize("fast-math")

#include <cstddef>
#include <vector>

#include <immintrin.h>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
	std::size_t rows_;
	std::size_t cols_;
	std::vector<double> data;

public:
	Grid(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols), data(rows * cols) {}

	double &operator()(std::size_t i, std::size_t j) {
		return data[i * cols_ + j];
	}
	double operator()(std::size_t i, std::size_t j) const {
		return data[i * cols_ + j];
	}

	std::size_t rows() const {
		return rows_;
	}
	std::size_t cols() const {
		return cols_;
	}
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
static void apply_stencil(const Grid &old_grid, Grid &new_grid) {
	const size_t N = old_grid.rows();
	const size_t M = old_grid.cols();

	for (int i = 1; i < N - 1; i++) {
		for (int j = 0; j < M; j++) {
			if (j == 0 || j == M - 1) {
				new_grid(i, j) = old_grid(i, j);
				continue;
			}
			new_grid(i, j) =
				0.5 * old_grid(i, j) + 0.125 * (old_grid(i - 1, j) + old_grid(i, j - 1) +
												old_grid(i + 1, j) + old_grid(i, j + 1));
		}
	}

	for (int j = 0; j < M; j++) {
		new_grid(0, j) = old_grid(0, j);
		new_grid(N - 1, j) = old_grid(N - 1, j);
	}
}
