#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>
#include <limits>
#include <new>
#include <type_traits>
#include <algorithm>
#include <optional>

inline constexpr std::size_t kGridAlignment = 64;

inline constexpr std::size_t kMinParallelCells = std::size_t{1} << 17;

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.

template <class T> struct AlignedAllocator {
    using value_type = T;
    using is_always_equal = std::true_type;

    AlignedAllocator() noexcept = default;

    template <class U> AlignedAllocator(const AlignedAllocator<U> &) noexcept {}

    T *allocate(std::size_t count) {
        static_assert(alignof(T) <= kGridAlignment);

        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_array_new_length();
        }

        return static_cast<T *>(
            ::operator new(count * sizeof(T), 
            std::align_val_t{kGridAlignment}));
    }

    void deallocate(T *pointer, std::size_t) noexcept {
        ::operator delete(pointer, std::align_val_t{kGridAlignment});
    }
};

template <class T, class U> constexpr bool operator==(
    const AlignedAllocator<T> &, 
    const AlignedAllocator<U> &
) noexcept {
    return true;
}

template <class T, class U> constexpr bool operator!=(
    const AlignedAllocator<T> &, 
    const AlignedAllocator<U> &
) noexcept {
    return false;
}

struct RowRange {
    std::size_t begin = 0;
    std::size_t end = 0;

    bool empty() const noexcept {return begin >= end;}
};

inline bool row_has_nonzero(const double *row, std::size_t cols) noexcept {
    bool any = false;
    for (std::size_t j=0; j<cols; ++j) {
        any |= (row[j] != 0.0);
    }
    return any;
}

class Grid {
  private:
    std::size_t rows_;
    std::size_t cols_;
    std::size_t stride_;

    std::vector<double, AlignedAllocator<double>> values_;

    // If set, every nonzero value lies in these rows. A new grid is all zeros,
    // so it starts as an empty range; any write through operator() clears it.
    std::optional<RowRange> nz_rows_ = RowRange{};

    static std::size_t padded_stride(std::size_t cols) {
        constexpr std::size_t multiple = (kGridAlignment / sizeof(double));
        if (cols > std::numeric_limits<std::size_t>::max() - (multiple - 1)) {throw std::length_error("Grid width too large");}
        return ((cols + multiple - 1) / multiple) * multiple;
    }

    static std::size_t storage_size(std::size_t rows, std::size_t stride) {
        if (stride != 0 && rows > std::numeric_limits<std::size_t>::max() / stride) {throw std::length_error("grid size too large");}
        return rows * stride;
    }

    double *row_data(std::size_t i) noexcept {return values_.data() + i * stride_;}

    // Rows containing every nonzero value. If unknown, scans inward from the
    // top and bottom; rows in between are never read.
    RowRange nonzero_rows() const noexcept {
        if (nz_rows_) return *nz_rows_;
        std::size_t begin = 0;
        while (begin < rows_ && !row_has_nonzero(row_data(begin), cols_)) {
            ++begin;
        }
        if (begin == rows_) return RowRange{};
        std::size_t end   = rows_;
        while (!row_has_nonzero(row_data(end - 1), cols_)) {
            --end;
        }

        return RowRange{begin, end};
    }

    void set_nonzero_rows(RowRange range) noexcept { nz_rows_ = range; }

    // apply_stencil writes rows directly and keeps nz_rows_ up to date
    friend void apply_stencil(const Grid &old_grid, Grid &new_grid);

  public:
    Grid(std::size_t rows, std::size_t cols)
        : rows_(rows), cols_(cols), stride_(padded_stride(cols)),
          values_(storage_size(rows, stride_), 0.0) {}

    double &operator()(std::size_t i, std::size_t j)      noexcept {
        nz_rows_.reset();
        return values_[i * stride_ + j];
    }
    double operator()(std::size_t i, std::size_t j) const noexcept {
        return values_[i * stride_ + j];
    }

    const double *row_data(std::size_t i)           const noexcept { 
        return values_.data() + i * stride_; 
    }

    std::size_t rows() const noexcept { return rows_; }
    std::size_t cols() const noexcept { return cols_; }
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.

inline void update_row(const double *__restrict__ north, const double *__restrict__ center,
                       const double *__restrict__ south, double *__restrict__ out,
                       std::size_t cols) noexcept {
#ifdef _OPENMP
#pragma omp simd aligned(north, center, south, out : kGridAlignment)
#endif
    for (std::size_t j = 1; j < cols - 1; ++j) {
        out[j] = 0.5 * center[j] + 0.125 * (north[j] + south[j] + center[j - 1] + center[j + 1]);
    }
}

inline RowRange grow(RowRange r, std::size_t rows) noexcept {
    if (r.empty())    return r;
    if (r.begin > 0)  --r.begin;
    if (r.end < rows) ++r.end;
    return r;
}

inline RowRange merge(RowRange a, RowRange b) noexcept {
    if (a.empty()) return b;
    if (b.empty()) return a;

    return RowRange{
        std::min(a.begin, b.begin), 
        std::max(a.end, b.end)
    };
}

inline void apply_stencil(const Grid &old_grid, Grid &new_grid) {
    // non-aliasing guarantee
    if (&old_grid == &new_grid) {
        throw std::invalid_argument("Input & output must be separate grids");
    }

    if (old_grid.rows() != new_grid.rows() || old_grid.cols() != new_grid.cols()) {
        throw std::invalid_argument("Grids must have the same dimension");
    }

    const std::size_t rows = old_grid.rows();
    const std::size_t cols = old_grid.cols();

    // safeguard
    if (rows == 0 || cols == 0) return;

    // top and bottom boundary rows are copied unchanged
    std::copy_n(old_grid.row_data(0),        cols, new_grid.row_data(0));
    std::copy_n(old_grid.row_data(rows - 1), cols, new_grid.row_data(rows - 1));

    const RowRange    active = grow(old_grid.nonzero_rows(), rows);
    const RowRange    todo   = merge(active, new_grid.nonzero_rows());
    const std::size_t first  = std::max<std::size_t>(todo.begin, 1);
    const std::size_t last   = std::min(todo.end, rows - 1);
    const std::size_t work   = last > first ? (last - first) * cols :0;

#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (work >= kMinParallelCells)
#endif
    for (std::size_t i = first; i < last; ++i) {
        const double *north  = old_grid.row_data(i - 1);
        const double *center = old_grid.row_data(i);
        const double *south  = old_grid.row_data(i + 1);
        double       *out    = new_grid.row_data(i);

        out[0]        = center[0];
        out[cols - 1] = center[cols - 1];

        update_row(north, center, south, out, cols);
    }

    new_grid.set_nonzero_rows(active);
}
