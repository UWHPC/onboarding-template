#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <emmintrin.h>
#include <mutex>
#include <new>
#include <thread>

#include <immintrin.h>

class Grid;

// Pretends to be a `double`
class Proxy {
private:
	Grid *g;
	size_t idx;

public:
	Proxy(Grid *g, size_t idx) : g(g), idx(idx) {}

	operator double() const;
	Proxy &operator=(double);
	Proxy &operator=(const Proxy &o) {
		return *this = double(o);
	}
};

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
	size_t rows_;
	size_t cols_;

	// 32-byte alignment MUST be ensured due to the use of VMOVAPD and VMOVDQA in the kernels
	// Elected to provide 64-byte alignment for caching purposes
	double *data_;
	// Technically not true fixed point anymore (Q isn't necessarily a power of 2)
	// Just generic quantization since conversion speed is not a factor, so we can squeeze out more
	// precision with arbitrary quantization factors
	uint32_t *fixed_;

	// Step budget until we must switch back to doubles to avoid error accumulation
	mutable size_t max_steps_ = 0;
	mutable size_t num_steps_ = 0;

	// The current data cannot be quantized
	mutable bool bad_ = false;
	// If true, the quantized buffer `fixed_` is up to date (`data_` MAY be outdated)
	mutable bool fixed_valid_ = false;

	mutable double min_ = +INFINITY;
	mutable double dequant_;

	friend class Proxy;

public:
	Grid(size_t rows, size_t cols) : rows_(rows), cols_(cols) {
		size_t float_size = rows * cols * sizeof(double);
		size_t fixed_size = rows * cols * sizeof(uint32_t);
		size_t align_fixed = (64 - float_size % 64) % 64;
		size_t alloc_size = float_size + align_fixed + fixed_size;

		data_ = (double *)::operator new(alloc_size, std::align_val_t(64));
		fixed_ = (uint32_t *)((uintptr_t)data_ + float_size + align_fixed);
		memset(data_, 0, alloc_size);
	}

	~Grid() {
		::operator delete(data_, std::align_val_t(64));
	}

	Grid(const Grid &) = delete;
	Grid &operator=(const Grid &) = delete;

	Proxy operator()(size_t i, size_t j) {
		return {this, i * cols_ + j};
	}
	double operator()(size_t i, size_t j) const {
		size_t idx = i * cols_ + j;
		if (!fixed_valid_)
			return data_[idx];

		return fixed_[idx] * dequant_ + min_;
	}

	double *get_float(size_t i, size_t j) {
		return &data_[i * cols_ + j];
	}
	const double *get_float(size_t i, size_t j) const {
		return &data_[i * cols_ + j];
	}
	uint32_t *get_fixed(size_t i, size_t j) {
		return &fixed_[i * cols_ + j];
	}
	const uint32_t *get_fixed(size_t i, size_t j) const {
		return &fixed_[i * cols_ + j];
	}

	size_t rows() const {
		return rows_;
	}
	size_t cols() const {
		return cols_;
	}
	bool odd() const {
		return num_steps_ % 2;
	}

	// Go to `double` mode and make `data_` the authoritative buffer
	void fallback() const {
		if (!fixed_valid_)
			return;

		const size_t stride = 256 / 8 / sizeof(double);
		const size_t S = rows_ * cols_;
		const size_t T = S % stride;

		const __m256d dequant = _mm256_set1_pd(dequant_);
		const __m256d offset = _mm256_set1_pd(min_);

		// Alignment guaranteed from alloc
		for (size_t i = 0; i < S - T; i += stride) {
			__m128i orig = _mm_load_si128((__m128i *)(fixed_ + i));
			// Signed conversion is fine since we quantized max to 0xfffffff8 / 4 (fits in int32)
			__m256d cvt = _mm256_cvtepi32_pd(orig);
			__m256d final = _mm256_fmadd_pd(cvt, dequant, offset);
			_mm256_store_pd(data_ + i, final);
		}
		for (size_t i = S - T; i < S; i++) {
			data_[i] = double(fixed_[i]) * dequant_ + min_;
		}

		fixed_valid_ = false;
	}

	// Go to quantized mode and make `fixed_` the authoritative buffer
	void promote() const {
		const size_t stride = 256 / 8 / sizeof(double);
		const size_t S = rows_ * cols_;
		const size_t T = S % stride;

		__m256d nan = _mm256_setzero_pd();
		__m256d max = _mm256_set1_pd(-INFINITY);
		__m256d min = _mm256_set1_pd(+INFINITY);
		for (size_t i = 0; i < S - T; i += stride) {
			__m256d val = _mm256_load_pd(data_ + i);
			nan = _mm256_or_pd(nan, _mm256_cmp_pd(val, val, _CMP_UNORD_Q));
			max = _mm256_max_pd(max, val);
			min = _mm256_min_pd(min, val);
		}
		if (__builtin_expect(_mm256_movemask_pd(nan) != 0, false)) {
			bad_ = true;
			return;
		}
		__m128d lo = _mm256_castpd256_pd128(max);
		__m128d hi = _mm256_extractf128_pd(max, 1);
		lo = _mm_max_pd(lo, hi);
		hi = _mm_unpackhi_pd(lo, lo);
		double max_ = _mm_cvtsd_f64(_mm_max_sd(lo, hi));
		lo = _mm256_castpd256_pd128(min);
		hi = _mm256_extractf128_pd(min, 1);
		lo = _mm_min_pd(lo, hi);
		hi = _mm_unpackhi_pd(lo, lo);
		min_ = _mm_cvtsd_f64(_mm_min_sd(lo, hi));
		for (size_t i = S - T; i < S; i++) {
			if (__builtin_expect(std::isnan(data_[i]), false)) {
				bad_ = true;
				return;
			}
			max_ = std::max(max_, data_[i]);
			min_ = std::min(min_, data_[i]);
		}

		double range = max_ - min_;
		if (std::fpclassify(range) != FP_NORMAL) {
			bad_ = true;
			return;
		}

		// The 4 tiles around must sum to <= 0xfffffff8 to leave room for the rounding factor
		// Why not 0xfffffffb? We limit the individual components to 0x3ffffffe so they sum to 0xfffffff8
		// If we limited them to 0x3fffffff instead, they sum to 0xfffffffc which is too big
		double quant_ = std::floor((double(0xfffffff8U) / 4) / range);
		dequant_ = 1.0 / quant_;

		const double ERR_GROWTH_RATE = 4e11; // Empirically found constant
		max_steps_ = std::fmin(std::floor(quant_ * quant_ / ERR_GROWTH_RATE), 1e18);
		if (max_steps_ == 0) {
			bad_ = true;
			return;
		}

		const __m256d quant = _mm256_set1_pd(quant_);
		const __m256d offset = _mm256_set1_pd(min_);

		// Alignment guaranteed from alloc
		for (size_t i = 0; i < S - T; i += stride) {
			__m256d orig = _mm256_load_pd(data_ + i);
			__m256d scaled = _mm256_mul_pd(quant, _mm256_sub_pd(orig, offset));
			// Signed conversion is fine since we quantized max to 0xfffffff8 / 4 (fits in int32)
			__m128i cvt = _mm256_cvtpd_epi32(scaled);
			_mm_store_si128((__m128i *)(fixed_ + i), cvt);
		}
		for (size_t i = S - T; i < S; i++) {
			fixed_[i] = std::nearbyint((data_[i] - min_) * quant_);
		}

		fixed_valid_ = true;
	}

	// Returns true if the fixed point representation is valid
	bool prerun() const {
		if (__builtin_expect(bad_, false))
			return false;

		if (max_steps_ == 0) {
			promote();
			if (bad_)
				return false;
			num_steps_ = 0;
		}

		if (num_steps_++ >= max_steps_) {
			fallback();
			bad_ = true;
			return false;
		} else {
			return true;
		}
	}

	void copy_metadata(const Grid &o) {
		max_steps_ = o.max_steps_;
		num_steps_ = o.num_steps_;
		min_ = o.min_;
		dequant_ = o.dequant_;
		bad_ = o.bad_;
		fixed_valid_ = o.fixed_valid_;
	}
};

Proxy::operator double() const {
	if (!g->fixed_valid_)
		return g->data_[idx];

	return g->fixed_[idx] * g->dequant_ + g->min_;
}

Proxy &Proxy::operator=(double x) {
	if (g->bad_ || g->num_steps_ != 0) {
		g->fallback();
		g->bad_ = false;
		g->num_steps_ = 0;
	}

	g->data_[idx] = x;
	g->max_steps_ = 0;

	return *this;
}

template <bool aligned>
static void apply_stencil_float(const Grid &old_grid, Grid &new_grid, size_t start, size_t end) {
	const size_t M = old_grid.cols();

	const double *__restrict__ old_data = old_grid.get_float(0, 0);
	double *__restrict__ new_data = new_grid.get_float(0, 0);

	if constexpr (!aligned) {
		if (M <= 2) {
			if (start < end)
				memcpy(new_data + start * M, old_data + start * M, (end - start) * M * sizeof(double));
			return;
		}
	}

	const size_t stride = 256 / 8 / sizeof(double);

	const __m256d two = _mm256_set1_pd(0.5);
	const __m256d eight = _mm256_set1_pd(0.125);

	for (size_t i = start; i < end; i++) {
		const double *row = old_data + (size_t)i * M;
		double *nrow = new_data + (size_t)i * M;

		if constexpr (aligned) {
			// Left and right can read out of bounds on the first/last iteration
			// This is fine because we own the memory there, it's just one row up/down
			// It only clobbers elements on the two edges, which we overwrite after
			for (size_t j = 0; j < M; j += stride) {
				__m256d up = _mm256_load_pd(row + j - M);
				__m256d down = _mm256_load_pd(row + j + M);
				__m256d left = _mm256_loadu_pd(row + j - 1);
				__m256d right = _mm256_loadu_pd(row + j + 1);
				__m256d cur = _mm256_load_pd(row + j);

				__m256d ver = _mm256_add_pd(up, down);
				__m256d hor = _mm256_add_pd(left, right);

				__m256d around = _mm256_mul_pd(_mm256_add_pd(ver, hor), eight);
				cur = _mm256_fmadd_pd(two, cur, around);

				_mm256_store_pd(nrow + j, cur);
			}
		} else {
			// I'm not sure if the differing loop bounds is actually necessary? I think we can actually just
			// do the exact same computation as above but using loadu and storeu everywhere.
			const size_t T = (M - 1) % stride;
			for (size_t j = 1; j < M - T; j += stride) {
				__m256d up = _mm256_loadu_pd(row + j - M);
				__m256d down = _mm256_loadu_pd(row + j + M);
				__m256d left = _mm256_loadu_pd(row + j - 1);
				__m256d right = _mm256_loadu_pd(row + j + 1);
				__m256d cur = _mm256_loadu_pd(row + j);

				__m256d ver = _mm256_add_pd(up, down);
				__m256d hor = _mm256_add_pd(left, right);

				__m256d around = _mm256_mul_pd(_mm256_add_pd(ver, hor), eight);
				cur = _mm256_fmadd_pd(two, cur, around);

				_mm256_storeu_pd(nrow + j, cur);
			}
			for (size_t j = M - T; j < M - 1; j++)
				nrow[j] = 0.5 * row[j] + 0.125 * (row[j - M] + row[j - 1] + row[j + M] + row[j + 1]);
		}

		// Fix the clobbered edges
		nrow[M - 1] = row[M - 1];
		nrow[0] = row[0];
	}
}

template <bool aligned>
static void apply_stencil_fixed(const Grid &old_grid, Grid &new_grid, size_t start, size_t end, bool odd) {
	const size_t M = old_grid.cols();

	const uint32_t *__restrict__ old_data = old_grid.get_fixed(0, 0);
	uint32_t *__restrict__ new_data = new_grid.get_fixed(0, 0);

	if constexpr (!aligned) {
		if (M <= 2) {
			if (start < end)
				memcpy(new_data + start * M, old_data + start * M, (end - start) * M * sizeof(uint32_t));
			return;
		}
	}

	const size_t stride = 256 / 8 / sizeof(uint32_t);

	// Bias cancellation: since round to even is too expensive and rounding halves up would incur tons
	// of extra error that we can't afford, we employ bias cancellation by alternating halves up/down
	// for each iteration.
	// Due to the ordering of function calls in `apply_stencil`, odd actually indicates whether the
	// **next** iteration is odd, so it's actually negated from the correct value. However, we don't care
	// about its actual value, we just need it to alternate between iterations.
	uint32_t _inner_round = odd ? 0 : 1;
	uint32_t _outer_round = odd ? 3 : 4;
	const __m256i inner_round = _mm256_set1_epi32(_inner_round);
	const __m256i outer_round = _mm256_set1_epi32(_outer_round);

	for (size_t i = start; i < end; i++) {
		const uint32_t *row = old_data + (size_t)i * M;
		uint32_t *nrow = new_data + (size_t)i * M;

		if constexpr (aligned) {
			// Left and right can read out of bounds on the first/last iteration
			// This is fine because we own the memory there, it's just one row up/down
			// It only clobbers elements on the two edges, which we overwrite after
			for (size_t j = 0; j < M; j += stride) {
				__m256i up = _mm256_load_si256((__m256i *)(row + j - M));
				__m256i down = _mm256_load_si256((__m256i *)(row + j + M));
				__m256i left = _mm256_loadu_si256((__m256i *)(row + j - 1));
				__m256i right = _mm256_loadu_si256((__m256i *)(row + j + 1));
				__m256i cur = _mm256_load_si256((__m256i *)(row + j));

				__m256i outer = _mm256_add_epi32(_mm256_add_epi32(up, down), _mm256_add_epi32(left, right));

				__m256i around = _mm256_srli_epi32(_mm256_add_epi32(outer, outer_round), 3);
				__m256i inner = _mm256_srli_epi32(_mm256_add_epi32(cur, inner_round), 1);

				cur = _mm256_add_epi32(inner, around);
				_mm256_store_si256((__m256i *)(nrow + j), cur);
			}
		} else {
			// I'm not sure if the differing loop bounds is actually necessary? I think we can actually just
			// do the exact same computation as above but using loadu and storeu everywhere.
			const size_t T = (M - 1) % stride;
			for (size_t j = 1; j < M - T; j += stride) {
				__m256i up = _mm256_loadu_si256((__m256i *)(row + j - M));
				__m256i down = _mm256_loadu_si256((__m256i *)(row + j + M));
				__m256i left = _mm256_loadu_si256((__m256i *)(row + j - 1));
				__m256i right = _mm256_loadu_si256((__m256i *)(row + j + 1));
				__m256i cur = _mm256_loadu_si256((__m256i *)(row + j));

				__m256i outer = _mm256_add_epi32(_mm256_add_epi32(up, down), _mm256_add_epi32(left, right));

				__m256i around = _mm256_srli_epi32(_mm256_add_epi32(outer, outer_round), 3);
				__m256i inner = _mm256_srli_epi32(_mm256_add_epi32(cur, inner_round), 1);

				cur = _mm256_add_epi32(inner, around);
				_mm256_storeu_si256((__m256i *)(nrow + j), cur);
			}
			for (size_t j = M - T; j < M - 1; j++)
				nrow[j] = ((row[j] + _inner_round) >> 1) +
						  ((row[j - M] + row[j - 1] + row[j + M] + row[j + 1] + _outer_round) >> 3);
		}

		// Fix the clobbered edges
		nrow[M - 1] = row[M - 1];
		nrow[0] = row[0];
	}
}

static void dispatch(
	const Grid &old_grid, Grid &new_grid, size_t start, size_t end, bool odd, bool fixed, bool aligned
) {
	if (fixed) {
		if (__builtin_expect(aligned, true))
			apply_stencil_fixed<true>(old_grid, new_grid, start, end, odd);
		else
			apply_stencil_fixed<false>(old_grid, new_grid, start, end, odd);
	} else {
		if (__builtin_expect(aligned, true))
			apply_stencil_float<true>(old_grid, new_grid, start, end);
		else
			apply_stencil_float<false>(old_grid, new_grid, start, end);
	}
}

struct alignas(64) ThreadInfo {
	struct {
		std::thread *thread;
		const Grid *old_grid;
		Grid *new_grid;
		size_t start, end;
		bool fixed, aligned;
	};
};

static constexpr int NTHREADS = 4;
static constexpr int MAX_SPIN = 500000;

// The following 3 are purposefully leaked since we don't have a proper way of cancelling workers
// and deleting a cv or mutex while workers are waiting is UB (caused hang on my machine)
alignas(64) static ThreadInfo tis[NTHREADS - 1];
static std::condition_variable *cv;
static std::mutex *mu;

static std::atomic_int done = 0;
static std::atomic_int epoch = 0;
static std::atomic_bool cancel = false;

// Returns `true` if the program is shutting down
static bool spin_then_sleep(int cur_epoch) {
	for (int spin = 0; spin < MAX_SPIN; spin++) {
		if (cancel.load(std::memory_order_acquire))
			return true;
		if (epoch.load(std::memory_order_acquire) >= cur_epoch)
			return false;
		_mm_pause();
	}

	std::unique_lock lk{*mu};
	cv->wait(lk, [&] {
		return cancel.load(std::memory_order_acquire) || epoch.load(std::memory_order_acquire) >= cur_epoch;
	});
	return cancel.load(std::memory_order_acquire);
}

static void worker(int idx) {
	const ThreadInfo &t = tis[idx];
	int cur_epoch = 0;

	while (true) {
		if (spin_then_sleep(++cur_epoch))
			return;

		dispatch(*t.old_grid, *t.new_grid, t.start, t.end, t.old_grid->odd(), t.fixed, t.aligned);

		done.fetch_add(1, std::memory_order_release);
	}
}

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
static void apply_stencil(const Grid &old_grid, Grid &new_grid) {
	const size_t N = old_grid.rows();
	const size_t M = old_grid.cols();

	const bool fixed = old_grid.prerun();
	const bool aligned = fixed ? (M % (256 / 32)) == 0 : (M % (256 / 64)) == 0;

	if (N >= 8) {
		size_t base = 1;
		const size_t stride = (N - 2) / NTHREADS;
		size_t extra = (N - 2) - stride * NTHREADS;

		for (size_t i = 0; i < NTHREADS - 1; i++) {
			size_t size = stride + (i < extra);

			tis[i] = {tis[i].thread, &old_grid, &new_grid, base, base + size, fixed, aligned};
			base += size;
		}

		done.store(0, std::memory_order_relaxed);
		{
			std::lock_guard lock(*mu);
			epoch.fetch_add(1, std::memory_order_release);
		}
		cv->notify_all();

		dispatch(old_grid, new_grid, base, N - 1, old_grid.odd(), fixed, aligned);
	} else {
		dispatch(old_grid, new_grid, 1, N - 1, old_grid.odd(), fixed, aligned);
	}

	if (fixed) {
		std::memcpy(new_grid.get_fixed(N - 1, 0), old_grid.get_fixed(N - 1, 0), M * sizeof(uint32_t));
		std::memcpy(new_grid.get_fixed(0, 0), old_grid.get_fixed(0, 0), M * sizeof(uint32_t));
	} else {
		std::memcpy(new_grid.get_float(N - 1, 0), old_grid.get_float(N - 1, 0), M * sizeof(double));
		std::memcpy(new_grid.get_float(0, 0), old_grid.get_float(0, 0), M * sizeof(double));
	}
	new_grid.copy_metadata(old_grid);

	if (N >= 8) {
		while (done.load(std::memory_order_acquire) < NTHREADS - 1)
			_mm_pause();
	}
}

__attribute__((constructor(65535))) static void init_workers() {
	cv = new std::condition_variable;
	mu = new std::mutex;

	for (int i = 0; i < NTHREADS - 1; i++) {
		tis[i].thread = new std::thread(worker, i);
	}
}

__attribute__((destructor(65535))) static void dtor_workers() {
	{
		std::lock_guard lock(*mu);
		cancel.store(true, std::memory_order_release);
	}
	cv->notify_all();

	for (int i = 0; i < NTHREADS - 1; i++) {
		tis[i].thread->join();
	}
}
