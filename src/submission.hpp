#pragma once

#include <cstddef>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <new>
#include <limits>

// I added an allocator to ensure alignment for the vector grid
// I used Struct instead of Class as everything should be public anyways
template <typename T, std::size_t Alignment>
struct AlignedAllocator{
  using value_type = T;

  // Rebind is needed here as Alignment is a non-type template param, requiring extra handling
  template <typename alternativeType>
  struct rebind{
    using other = AlignedAllocator<alternativeType, Alignment>;
  };
  
  AlignedAllocator() noexcept = default;

  // Standard function names that are called by vector allocation
  template <typename U>
  AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

  T* allocate (std::size_t n){
    if (n == 0) {
      return nullptr;
    }
    // Checks if n * sizeof(T) overflows size_t, which would cause less memory to be allocated then needed
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)){
      throw std::bad_alloc();
    }
    return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{Alignment}));
  }
  void deallocate(T* p, std::size_t) noexcept{
    ::operator delete(p, std::align_val_t{Alignment});
  }

  template <typename U> bool operator == (const AlignedAllocator<U, Alignment>&) const noexcept{
    return true;
  }
  template <typename U> bool operator != (const AlignedAllocator<U, Alignment>&) const noexcept{
    return false;
  }
};

class Grid {
private:
  // 64 byte alignment for cache-line alignment and SIMD access
  // kAlignment is kept private as it's specific to only Grid's layout
  static constexpr std::size_t kAlignment = 64;
  std::size_t rows_;
  std::size_t cols_;
  std::size_t stride_;
  std::vector<double, AlignedAllocator<double, kAlignment>> data_matrix;

  static std::size_t calculate_stride(std::size_t columns){
    constexpr std::size_t grids_per_row = kAlignment / sizeof(double);
    std::size_t remainder = columns % grids_per_row;
    if (remainder == 0){
      return columns;
    }

    std::size_t padding = grids_per_row - remainder;
    if (columns > std::numeric_limits<std::size_t>::max() - padding){
      throw std::overflow_error("Grid stride overflows size_t");
    }

    return columns + padding;
  }

  // Checks if rows * stride overflows size_t, which could cause std::vector to be smaller than needed
  static std::size_t check_overflow(std::size_t rows, std::size_t stride){
    if ((rows != 0) and (stride > std::numeric_limits<std::size_t>::max() / rows)){
      throw std::overflow_error("Grid rows * stride overflows size_t");
    }
    return rows * stride;
  }

public:
  // Switched to member initialization to prevent double allocation
  Grid(std::size_t rows, std::size_t cols): 
    rows_(rows),
    cols_(cols),
    stride_(calculate_stride(cols)),
    data_matrix(check_overflow(rows,stride_), 0.0)
    {}
  
  // I chose 1D vector as it ensures data is contiguous for later optimization.
  double& operator()(std::size_t i, std::size_t j){
    if ((i >= rows_) or (j >= cols_)) throw std::out_of_range("Grid operator (,) index out of range");
    return data_matrix[i * stride_ + j];
  }
  double operator()(std::size_t i, std::size_t j) const{
    if ((i >= rows_) or (j >= cols_)) throw std::out_of_range("Grid operator (,) index out of range");
    return data_matrix[i * stride_ + j];
  }

  struct View {
    double* data;
    std::size_t rows, cols, stride;
  };
  struct ConstView {
    const double* data;
    std::size_t rows, cols, stride;
  };

  View view() {
    return View{data_matrix.data(), rows_, cols_, stride_};
  }
  ConstView view() const {
    return ConstView{data_matrix.data(), rows_, cols_, stride_};
  }

};

// Inline allows the function to be called by multiple cpp files
inline void apply_stencil(const Grid& old_grid, Grid& new_grid){
  // although this is expensive, it's only for a one-time edge case
  if (&old_grid == &new_grid){
    Grid old_grid_snapshot = old_grid;
    apply_stencil(old_grid_snapshot, new_grid);
    return;
  }

  Grid::ConstView old_view = old_grid.view();
  Grid::View new_view = new_grid.view();

  if ((old_view.rows != new_view.rows) or (old_view.cols != new_view.cols)){
    throw std::invalid_argument("old_grid and new_grid dimensions differ :(");
  }

  std::size_t columns = old_view.cols;
  std::size_t rows = old_view.rows;
  std::size_t stride = old_view.stride;

  // Small grid handling! Don't need complex logic if all grids are boundary grids
  if ((rows == 0) or (columns == 0)){
    return;
  }
  if ((rows < 3) or (columns < 3)){
    #pragma omp parallel for schedule(static)
    for (std::size_t i = 0; i < rows; ++i){
      std::memcpy(new_view.data + i * stride, 
                  old_view.data + i * stride, 
                  columns * sizeof(double));
    }
    return;
  }

  #pragma omp parallel for schedule(static) 
  for (std::size_t i = 1; i < rows - 1; i++){
    const double* __restrict row_mid = old_view.data + i * stride;
    const double* __restrict row_up = old_view.data + (i - 1) * stride;
    const double* __restrict row_down = old_view.data + (i + 1) * stride;
    double* __restrict out = new_view.data + i * stride;
    //__restrict on each row for easier compiler vectorization

    //Take the chance to copy the boundary columns as well within the threaded loop
    out[0] = row_mid[0];
    out[columns - 1] = row_mid[columns - 1];

    // j starts at 1, so first element may not be SIMD aligned,
    // the compiler can apply peeling or unaligned vector access as required.
    #pragma omp simd
    for (std::size_t j = 1; j < columns - 1; j++){
      out[j] = 0.5 * row_mid[j] +
             0.125 * (row_down[j] + row_up[j] +
                     row_mid[j-1] + row_mid[j+1]);
    }
  }

  // Use std::memcpy for contiguous non-overlapping ranges, more explicit than std::copy
  std::memcpy(new_view.data, old_view.data, columns * sizeof(double));
  std::memcpy(new_view.data + (rows-1) * stride, 
              old_view.data + (rows-1) * stride, 
              columns * sizeof(double));

}