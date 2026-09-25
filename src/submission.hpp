#pragma once
#include <cstdlib> 
#include <cstddef>
#include <vector>     
#include <new>

// A View holds pointers and metadata
// passed by value to kernel - fits in CPU registers. 
template<typename T> 
struct GridViewT { 
  T* data; 
  std::size_t    rows; 
  std::size_t    cols; 
  std::size_t    stride; // row width after padding (SIMD)

  // row pointer to the start of row i in memory address 
  T* row(std::size_t i) const noexcept { 
    return data + i * stride; 
  } 

  // value of data 
  T& operator() (std::size_t i, std::size_t j) const { 
    return data[i * stride + j];
  }
}; 

// View objects for mutable/immutable access
using GridView = GridViewT<double>; // read-write 
using ConstGridView = GridViewT<const double>; // read-only


// AlignedAllocator struct - garauntees 64 byte alignment from vector.
template<typename T, std::size_t Alignment = 64>
struct AlignedAllocator{
  using value_type = T; 

  // allocate memory given # of doubles
  T* allocate(std::size_t n){
    std::size_t bytes = n * sizeof(T);
    
    // Round bytes up match cache line alignment
    bytes = ((bytes + Alignment - 1) / Alignment) * Alignment; 
    void* ptr = std::aligned_alloc(Alignment, bytes); 
    if (!ptr) throw std::bad_alloc(); // fallback
    return static_cast<T*>(ptr); 
  }

  // free memory using RAII
  void deallocate(T* p, std::size_t) noexcept {
    std::free(p); 
  }

  // allow vector to rebind allocator
  template<typename U> 
  struct rebind {using other = AlignedAllocator<U, Alignment>;}; 

  bool operator==(const AlignedAllocator&) const noexcept { return true; }
  bool operator!=(const AlignedAllocator&) const noexcept { return false; }
};


// Grid Class  
class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::size_t stride_;

  // Aligned Allocation - RAII with std::vector
  std::vector<double, AlignedAllocator<double, 64>> flat_data_; 
  static std::size_t compute_padded_stride(std::size_t cols) {
      return ((cols + 7) / 8) * 8;
  } 
  
public:
  Grid(std::size_t rows, std::size_t cols) : 
    rows_(rows), cols_(cols), stride_(compute_padded_stride(cols)), flat_data_(rows * stride_, 0.0){}; // zero init

  // read-write operator + & means reference to i,j cell value
  double& operator()(std::size_t i, std::size_t j){
    return flat_data_[i * stride_  + j]; 
  } 

  double  operator()(std::size_t i, std::size_t j) const {
    return flat_data_[i * stride_  + j]; 
  }

  // Expose dimensions so apply_stencil can loop over them
  std::size_t rows() const { return rows_; }
  std::size_t cols() const { return cols_; }
  std::size_t stride() const noexcept { return stride_; }


  GridView view() { return {flat_data_.data(), rows_, cols_, stride_};}
  ConstGridView view() const {return {flat_data_.data(), rows_, cols_, stride_}; }
  
};  

// kenel row operations
inline void stencil_row_kernel(

    // restrict applied to garauntee no two pointers overlap, emit SIMD code
    const double* __restrict__ r_prev, 
    const double* __restrict__ r_curr, 
    const double* __restrict__ r_next, 
    double* __restrict__ r_out,
    std::size_t cols  
) noexcept{

  // copy left/right boundaries
  r_out[0] = r_curr[0];
  r_out[cols-1] = r_curr[cols-1]; 

  // calculate stencil updates for inner cols
  #pragma omp simd 
  for(std::size_t j=1; j<cols-1; j++){
    // 3 point add, 2 point multiply
    double neighbors = (r_prev[j] + r_next[j]) + (r_curr[j-1] + r_curr[j+1]); 
    r_out[j] = 0.5 * r_curr[j] + 0.125 * neighbors; 
  }
  
}

inline void apply_stencil(ConstGridView old_v, GridView new_v){ 
  const auto R = old_v.rows;
  const auto C = old_v.cols;


  // Top and Bottom boundary rows
  const double* __restrict__ top_in  = old_v.row(0);
  double*       __restrict__ top_out = new_v.row(0);
  for (std::size_t j = 0; j < C; ++j) {
    top_out[j] = top_in[j];
  }
  const double* __restrict__ bot_in  = old_v.row(R-1);
  double*       __restrict__ bot_out = new_v.row(R-1);
  for (std::size_t j = 0; j < C; ++j) {
    bot_out[j] = bot_in[j];
  }

  // thread starts 
  #pragma omp parallel for schedule(static) 
  for (std::size_t i=1; i<R-1; i++){
    // stencil function for rows, using pointers 
    stencil_row_kernel(old_v.row(i-1), old_v.row(i), old_v.row(i+1), new_v.row(i), C);
  }
}

inline void apply_stencil(const Grid& old_g, Grid& new_g) {
  apply_stencil(old_g.view(), new_g.view());
}