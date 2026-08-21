#pragma once

#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <new>
#include <sys/mman.h>

#include <cstddef>

#pragma GCC optimize("fast-math")

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
	std::size_t rows_;
	std::size_t cols_;
	std::size_t size_;
	double *data;

public:
	Grid(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols) {
		constexpr std::size_t align = 0x200000; // THP page size
		size_ = rows * cols * sizeof(double);
		std::size_t req = size_ + align;

		void *raw = mmap(nullptr, req, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (raw == MAP_FAILED)
			throw std::bad_alloc();

		std::uintptr_t addr = (std::uintptr_t)raw;
		std::uintptr_t aligned = (addr + align - 1) & ~(align - 1);
		std::size_t front_slack = aligned - addr;
		std::size_t back_slack = req - front_slack - size_;

		if (front_slack)
			munmap(raw, front_slack);
		if (back_slack)
			munmap((void *)(aligned + size_), back_slack);

		raw = (void *)aligned;
		data = (double *)aligned;
		madvise(data, size_, MADV_SEQUENTIAL | MADV_UNMERGEABLE | MADV_HUGEPAGE | MADV_COLLAPSE);
		std::memset(data, 0, size_);
	}

	~Grid() {
		munmap(data, size_);
	}

	Grid(const Grid &) = delete;
	Grid &operator=(const Grid &) = delete;

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
			new_grid(i, j) = 0.5 * old_grid(i, j) + 0.125 * (old_grid(i - 1, j) + old_grid(i, j - 1) +
															 old_grid(i + 1, j) + old_grid(i, j + 1));
		}
	}

	for (int j = 0; j < M; j++) {
		new_grid(0, j) = old_grid(0, j);
		new_grid(N - 1, j) = old_grid(N - 1, j);
	}
}
