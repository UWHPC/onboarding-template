#pragma once

#include <cstddef>
#include <cstring>
#include <cassert>
#include <array>
#include <vector>
#include <new>

// non-owning view: index Grid data via shape/strides without copying the (large) buffer
template <typename T, std::size_t N>
struct View {
    T* data;
    std::array<std::size_t, N> shape;
    std::array<std::size_t, N> strides;

    // accessed like v(a, b, c, ..)
    template <typename... Indices>
    T& operator()(Indices... indices) const {
        static_assert(sizeof...(Indices) == N);

        const std::array<std::size_t, N> index{static_cast<std::size_t>(indices)...};

        std::size_t offset{0};

        for (std::size_t i = 0; i < N; ++i){
            offset += index[i] * strides[i];
        }

        return data[offset];
    }
};

// match the 64B cache line: an aligned row start keeps each vectorized load (32B under AVX2) contained within one cache line
static constexpr std::size_t kCacheLine = 64;
static constexpr std::size_t kLaneElements = kCacheLine / sizeof(double);

// hot loop starts at col 1; prefix (lane - 1) doubles so col 1 is 64B-aligned
static constexpr std::size_t kRowPrefix = kLaneElements - 1;

// on my CPU (6-core i7-1365U):
//
//   cells (rows*cols)   serial us   2-thread us
//        2304 (48x48)       0.83        1.22
//        4096 (64x64)       2.32        1.91
//        9216 (96x96)       3.28        3.58
//       16384 (128x128)     7.72        5.90
static constexpr std::size_t kParallelMinCells = 4096;

// round up to next multiple of align (must be power of 2)
static constexpr std::size_t round_up(std::size_t n, std::size_t align) noexcept {
    // "overshoot" by (align - 1), then zero lower bits to ensure a multiple of align
    return (n + align - 1u) & ~(align - 1u);
}

// pad rows to whole cache lines so advancing by stride keeps every row's col 1 on the same 64B alignment
static std::size_t padded_stride(std::size_t cols) noexcept {
    return round_up((cols + kRowPrefix) * sizeof(double), kCacheLine) / sizeof(double);
}

// minimal allocator to give std::vector a cache-line-aligned base
template <typename T, std::size_t Alignment>
struct AlignedAllocator {
    using value_type = T;   // element type
    static_assert((Alignment & (Alignment - 1)) == 0, "alignment must be a power of two");
    static_assert(Alignment >= alignof(T));

    AlignedAllocator() noexcept = default;
    // allows a container to rebuild this allocator for a different type
    template <typename U>
    AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    T* allocate(std::size_t n) {
        return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{Alignment}));
    }
    void deallocate(T* p, std::size_t) noexcept {
        ::operator delete(p, std::align_val_t{Alignment});
    }

    template <typename U> struct rebind { using other = AlignedAllocator<U, Alignment>; };
};

// stateless, so any instance can free another's memory => always equal
template <typename T, typename U, std::size_t A>
bool operator==(const AlignedAllocator<T, A>&, const AlignedAllocator<U, A>&) noexcept { return true; }
template <typename T, typename U, std::size_t A>
bool operator!=(const AlignedAllocator<T, A>&, const AlignedAllocator<U, A>&) noexcept { return false; }

class Grid {
private:
    // RAII: std::vector ties buffer's lifetime to the object, freeing it automatically on destruction
    // 2nd arg is the allocator; default std::allocator only aligns to 16B but we want 64B-aligned
    using Storage = std::vector<double, AlignedAllocator<double, kCacheLine>>;

    std::array<std::size_t, 2> shape_;
    std::array<std::size_t, 2> strides_;
    Storage data_;
public:
    Grid(std::size_t rows, std::size_t cols)
        : shape_{rows, cols}
        , strides_{padded_stride(cols), 1}
        , data_(kRowPrefix + rows * strides_[0]) {}  // () value-inits to 0.0

    // skip kRowPrefix so callers index from logical (0,0); alignment padding stays hidden
    // r/w
    double& operator()(std::size_t row, std::size_t col){
        return data_[kRowPrefix + row * strides_[0] + col];
    }

    // read-only
    const double& operator()(std::size_t row, std::size_t col) const{
        return data_[kRowPrefix + row * strides_[0] + col];
    }

    // # of elements per dimension
    const std::array<std::size_t, 2>& shape() const{
        return shape_;
    }

    // # of elements to move in memory by dimension
    const std::array<std::size_t, 2>& strides() const{
        return strides_;
    }

    // r/w
    View<double, 2> view(){
        return {data_.data() + kRowPrefix, shape_, strides_};
    }

    // read-only
    View<const double, 2> view() const{
        return {data_.data() + kRowPrefix, shape_, strides_};
    }
};

// Copy top/bottom boundary rows from old_grid to new_grid.
inline void copy_boundaries(const double* __restrict old_data, double* __restrict new_data, std::size_t rows, std::size_t cols, std::size_t stride) noexcept {
    if (rows == 0 || cols == 0) return;

    // memcpy avoids per-element stride arithmetic and View overhead (vs. for loop + assignment)
    std::memcpy(new_data, old_data, cols * sizeof(double));
    if (rows > 1)
        std::memcpy(new_data + (rows-1) * stride, old_data + (rows-1) * stride, cols * sizeof(double));

    // everything is a boundary for 1 or 2 col. grids, copy the interior rows too
    if (cols < 3) {
        for (std::size_t i = 1; i + 1 < rows; ++i)
            std::memcpy(new_data + i * stride, old_data + i * stride, cols * sizeof(double));
    }
}

// Applies five-point stencil over all interior rows [1, rows-1).
inline void apply_stencil_interior(const double* __restrict old_data, double* __restrict new_data, std::size_t rows, std::size_t cols, std::size_t stride) noexcept {
    assert(rows >= 3 && cols >= 3);          // caller guarantees this; prevents cols-2 unsigned underflow
    const std::size_t interior_cols = cols - 2;

    // parallelism: rows are independent and cost the same (reads old_grid), schedule(static) splits them in equal chunks without sync nor load-balancing
    //      only if it's "worth it", we don't want the extra overhead for smaller/"trivial" grids
    #pragma omp parallel for schedule(static) if (rows * cols >= kParallelMinCells)
    for (std::size_t i = 1; i < rows - 1; ++i){
        // shift each row pointer to column 1 (64B-aligned) so it becomes offset 0
        const double* cur1 = old_data +  i * stride + 1;        // curr
        const double* above1 = old_data + (i - 1) * stride + 1; // above neighbour
        const double* below1 = old_data + (i + 1) * stride + 1; // row below neighbour
        const double* left1  = cur1 - 1;                        // left neighbour
        const double* right1 = cur1 + 1;                        // right neighbour

        double* dst1 = new_data +  i * stride + 1;              // destination

        // left/right boundary cells are copied (unchanged)
        dst1[-1] = cur1[-1];                // leftmost col
        dst1[cols - 2] = cur1[cols - 2];    // rightmost col

        // vectorization: compiler packs 4 doubles per AVX2 instruction cleanly
        // hot path: writes start at col 1, aligned(...:64) signals to compiler to emit faster aligned SIMD loads/stores instead of unaligned ones
        #pragma omp simd aligned(cur1, above1, below1, dst1 : kCacheLine)
        for (std::size_t j = 0; j < interior_cols; ++j){
            dst1[j] =
                0.5  * cur1[j]          // center
                + 0.125 * (
                    above1[j]
                    + below1[j]
                    + left1[j]
                    + right1[j]
                );
        }
    }
}

// Apply the five-point stencil over all interior points
// Keep boundary values unchanged from old_grid to new_grid
void apply_stencil(const Grid& old_grid, Grid& new_grid){
    const auto& [rows, cols] = old_grid.shape();
    const auto& [stride, col_stride] = old_grid.strides();
    assert(col_stride == 1);

    // __restrict promises old/new never alias, letting the compiler vectorize without reload guards
    const double* __restrict old_data = old_grid.view().data;
    double* __restrict new_data = new_grid.view().data;

    copy_boundaries(old_data, new_data, rows, cols, stride);

    // no interior
    if (rows < 3 || cols < 3) return;

    apply_stencil_interior(old_data, new_data, rows, cols, stride);
}
