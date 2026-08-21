#pragma once

#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <mutex>
#include <new>
#include <sys/mman.h>

#include <cstddef>
#include <thread>
#include <xmmintrin.h>

#pragma GCC optimize("fast-math")

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
	size_t rows_;
	size_t cols_;
	size_t size_;
	double *data_;

public:
	Grid(size_t rows, size_t cols) : rows_(rows), cols_(cols) {
		constexpr size_t align = 0x200000; // THP page size
		size_ = rows * cols * sizeof(double);
		size_t req = size_ + align;

		void *raw = mmap(nullptr, req, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (raw == MAP_FAILED)
			throw std::bad_alloc();

		uintptr_t addr = (uintptr_t)raw;
		uintptr_t aligned = (addr + align - 1) & ~(align - 1);
		size_t front_slack = aligned - addr;
		size_t back_slack = req - front_slack - size_;

		if (front_slack)
			munmap(raw, front_slack);
		if (back_slack)
			munmap((void *)(aligned + size_), back_slack);

		raw = (void *)aligned;
		data_ = (double *)aligned;
		madvise(data_, size_, MADV_SEQUENTIAL | MADV_UNMERGEABLE | MADV_HUGEPAGE | MADV_COLLAPSE);
		memset(data_, 0, size_);
	}

	~Grid() {
		munmap(data_, size_);
	}

	Grid(const Grid &) = delete;
	Grid &operator=(const Grid &) = delete;

	double &operator()(size_t i, size_t j) {
		return data_[i * cols_ + j];
	}
	double operator()(size_t i, size_t j) const {
		return data_[i * cols_ + j];
	}
	const double &get(size_t i, size_t j) const {
		return data_[i * cols_ + j];
	}

	size_t rows() const {
		return rows_;
	}
	size_t cols() const {
		return cols_;
	}
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
static void _apply_stencil(const Grid &old_grid, Grid &new_grid, size_t start, size_t end) {
	const size_t N = old_grid.rows();
	const size_t M = old_grid.cols();
	const size_t stride = 256 / 8 / sizeof(double);
	const size_t T = (M - 2) % stride;

	const __m256d two = _mm256_set1_pd(0.5);
	const __m256d eight = _mm256_set1_pd(0.125);

	for (int i = start; i < end; i++) {
		for (int j = 1; j < M - 2 - T; j += stride) {
			__m256d up = _mm256_loadu_pd(&old_grid.get(i - 1, j));
			__m256d down = _mm256_loadu_pd(&old_grid.get(i + 1, j));
			__m256d left = _mm256_loadu_pd(&old_grid.get(i, j - 1));
			__m256d right = _mm256_loadu_pd(&old_grid.get(i, j + 1));
			__m256d cur = _mm256_loadu_pd(&old_grid.get(i, j));

			__m256d ver = _mm256_add_pd(up, down);
			__m256d hor = _mm256_add_pd(left, right);

			__m256d around = _mm256_mul_pd(_mm256_add_pd(ver, hor), eight);
			cur = _mm256_fmadd_pd(two, cur, around);

			_mm256_storeu_pd(&new_grid(i, j), cur);
		}
		for (int j = M - 2 - T; j < M - 1; j++) {
			new_grid(i, j) = 0.5 * old_grid(i, j) + 0.125 * (old_grid(i - 1, j) + old_grid(i, j - 1) +
															 old_grid(i + 1, j) + old_grid(i, j + 1));
		}

		if (i != end - 1) {
			_mm_storeu_pd(&new_grid(i, M - 1), _mm_loadu_pd(&old_grid.get(i, M - 1)));
		}
	}
	new_grid(start, 0) = old_grid(start, 0);
	new_grid(end - 1, M - 1) = old_grid(end - 1, M - 1);
}

struct ThreadInfo {
	const Grid *old_grid;
	Grid *new_grid;
	size_t start, end;
};

class Barrier {
private:
	std::mutex mu;
	std::condition_variable cv;
	int num;
	int arrived;
	size_t gen_;

public:
	explicit Barrier(int num) : num(num), arrived(0), gen_(0) {}

	void arrive_and_wait() {
		std::unique_lock<std::mutex> lock(mu);
		std::size_t gen = gen_;
		if (++arrived == num) {
			arrived = 0;
			gen_++;
			cv.notify_all();
		} else {
			cv.wait(lock, [&] { return gen_ != gen; });
		}
	}
};

static constexpr int NTHREADS = 4;
static ThreadInfo tis[NTHREADS];

static inline Barrier &start_barrier() {
	static Barrier *b = new Barrier(NTHREADS + 1);
	return *b;
}
static inline Barrier &done_barrier() {
	static Barrier *b = new Barrier(NTHREADS + 1);
	return *b;
}

static void worker(int idx) {
	while (true) {
		const ThreadInfo &t = tis[idx];

		start_barrier().arrive_and_wait();
		_apply_stencil(*t.old_grid, *t.new_grid, t.start, t.end);
		done_barrier().arrive_and_wait();
	}
}

static void apply_stencil(const Grid &old_grid, Grid &new_grid) {
	const size_t N = old_grid.rows();
	const size_t M = old_grid.cols();

	if (N >= 8) {
		size_t base = 1;
		const size_t stride = (N - 2) / NTHREADS;

		for (int i = 0; i < NTHREADS - 1; i++) {
			tis[i] = {&old_grid, &new_grid, base, base + stride};
			base += stride;
		}
		tis[NTHREADS - 1] = {&old_grid, &new_grid, base, N - 1};

		start_barrier().arrive_and_wait();
		done_barrier().arrive_and_wait();
	} else {
		_apply_stencil(old_grid, new_grid, 1, N - 1);
	}

	for (int j = 0; j < M; j++) {
		new_grid(0, j) = old_grid(0, j);
		new_grid(N - 1, j) = old_grid(N - 1, j);
	}
}

__attribute__((constructor)) static void init_workers() {
	for (int i = 0; i < NTHREADS; i++) {
		std::thread(worker, i).detach();
	}
}
