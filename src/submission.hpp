#pragma once

#include <cstddef>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <mm_malloc.h> //portability, couldn't align with std::aligned_alloc coz windows or smth
#include <experimental/simd> //17?
// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.

namespace stdx = std::experimental;

class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::size_t stride_;

  static constexpr std::size_t ALIGNMENT = 64; // assumption, to change as needed
  //flat
  double* data_ = nullptr;

  // Helper to calculate padded stride
  static std::size_t calculate_stride(std::size_t cols) {
    // Round up cols to next multiple of 8 doubles (64 bytes)
    return (cols + 7) & ~std::size_t(7);
  }



public:
  Grid(std::size_t rows, std::size_t cols):
  rows_(rows), cols_(cols), stride_(calculate_stride(cols)) {
    std::size_t bytes = rows * stride_ * sizeof(double);
    data_ = static_cast<double*>(_mm_malloc(bytes, ALIGNMENT));
  }

  Grid(const Grid&) = delete;
  Grid& operator=(const Grid&) = delete;

  //move over copy
  Grid(Grid&&other) noexcept : rows_(other.rows_), cols_(other.cols_), stride_(other.stride_), data_(other.data_) {
    other.data_ = nullptr;
  }
  Grid& operator=(Grid&& other) noexcept {
    if (this != &other) {
      _mm_free(data_);
      rows_ = other.rows_;
      cols_ = other.cols_;
      stride_ = other.stride_;
      data_ = other.data_;
      other.data_ = nullptr;
    }
    return *this;
}

  ~Grid() {_mm_free(data_);}


  double& operator()(std::size_t i, std::size_t j) {
    return data_[i*stride_ +j];
  }
  double  operator()(std::size_t i, std::size_t j) const {
    return data_[i*stride_ +j];
  }

  std::size_t rows() const { return rows_; }
  std::size_t cols() const { return cols_; }
  std::size_t stride() const { return stride_;}

  double* data() { return &data_[0]; }
  const double* data() const { return &data_[0]; }//ptr to start
};

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  const std::size_t rows = old_grid.rows();
  const std::size_t cols = old_grid.cols();
  const std::size_t stride = old_grid.stride();
  //edge case for a grid with only boundaries
  if (rows <= 2 || cols <= 2) {
    std::memcpy(new_grid.data(), old_grid.data(), rows*stride*sizeof(double));
    return;
  }
  //unique location
  const double* __restrict__ src = old_grid.data();
  double* __restrict__ dst = new_grid.data();

  //copy boundaries(top/bottom)
  std::memcpy(dst,src, cols*sizeof(double)); //index 0
  std::memcpy(dst + (rows-1) * stride, src + (rows-1)*stride,cols*sizeof(double));

  //detect no. lanes and form the  vector object
  using V = stdx::native_simd<double>;
  constexpr std::size_t LANES = V::size();
  const V v_half(0.5); //current cell
  const V v_eighth(0.125); //surrounding four


#pragma omp parallel for schedule(static) //if (rows > 256) //cmake 18-21
    for (size_t i = 1; i < rows - 1; ++i) {
      //calculate where the row starts
      const double* __restrict__ r_top  = src + (i - 1) * stride;
      const double* __restrict__ r_curr = src + i * stride;
      const double* __restrict__ r_bot  = src + (i + 1) * stride;
      double* __restrict__       r_dst  = dst + i * stride;



      std::size_t j = 0;
      //loop thru cols
      for (; j + LANES <= cols-1; j += LANES) {
        if (j == 0)
          r_dst[0] = r_curr[0]; //copy the left boundary element
        V c;      c.copy_from(r_curr + j, stdx::vector_aligned);
        V top;    top.copy_from(r_top + j, stdx::vector_aligned);
        V bot;    bot.copy_from(r_bot + j, stdx::vector_aligned);
        V left;   left.copy_from(r_curr + j - 1, stdx::element_aligned); //around the cell
        V right;  right.copy_from(r_curr + j + 1, stdx::element_aligned);

        //0.5 * original + 0.125(top + bot + left + right)
        V sum_neighbors = top + bot + left + right;
        V res = v_half * c + v_eighth * sum_neighbors;
        res.copy_to(r_dst + j + 1, stdx::vector_aligned);
      }
      //Cleaner upper loop
      for (; j < cols-1; ++j) {
        r_dst[j] = 0.5 * r_curr[j] +
          0.125 * (r_top[j] + r_bot[j] + r_curr[j-1] + r_curr[j+1]);
      }
      //rightmost
      r_dst[cols-1] = r_curr[cols-1];
    }

}