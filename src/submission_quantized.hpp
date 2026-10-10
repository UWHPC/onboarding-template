/*
    Hi! This is my attempt on making a quantized version of the stencil kernel! Unfortunately, 
    it didn't work out because the temperate range was too much for my quantization to fit in the 
    1e-6 error range. Thus, the kernel kept defaulting to the regular double implementation to prevent 
    the error bound! However, I still wanted to post this work regardless because under circumstances
    that the quantized kernel do occur, it performs like 2x faster than the regular double kernel on my
    laptop. 

    I don't really want it to go to waste, BUT in the process, it made me reexamine how the starting 
    grids are actually geneerated and ugh, it directly led to a cheese with the current submission.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <stdint.h>
#include <vector>


// This is the exact same as my regular submission. 
template <typename T>
class PaddedAlignedVector {
private:
  static constexpr std::size_t CACHE_ALIGNMENT{64};

  static_assert(CACHE_ALIGNMENT % sizeof(T) == 0);
  static constexpr std::size_t elements_per_line{CACHE_ALIGNMENT / sizeof(T)};
  template <typename U>
  class CacheAlignAllocator {
  public:
    using value_type = U;

    CacheAlignAllocator() noexcept = default;

    template <typename V>
    CacheAlignAllocator(const CacheAlignAllocator<V>&) noexcept {}

    template <typename V>
    bool operator==(const CacheAlignAllocator<V>&) const noexcept {
      return true;
    }

    template <typename V>
    bool operator!=(const CacheAlignAllocator<V>&) const noexcept {
      return false;
    }

    U* allocate(std::size_t count) {
      if (count > std::numeric_limits<std::size_t>::max() / sizeof(U)) {
        throw std::bad_array_new_length{};
      }

      return static_cast<U*>(::operator new(count * sizeof(U), std::align_val_t{CACHE_ALIGNMENT}));
    }

    void deallocate(U* pointer, std::size_t) noexcept {
      ::operator delete(pointer, std::align_val_t{CACHE_ALIGNMENT});
    }
  };

  std::size_t stride_;

  std::vector<T, CacheAlignAllocator<T>> storage_;

public:
  PaddedAlignedVector(std::size_t rows, std::size_t cols) : stride_{0} {
    constexpr auto max_size{std::numeric_limits<std::size_t>::max()};
    if (cols > max_size - (elements_per_line - 1)) {
      throw std::bad_array_new_length{};
    }

    stride_ = (cols + elements_per_line - 1) / elements_per_line * elements_per_line;

    if (stride_ != 0 && rows > (max_size / sizeof(T)) / stride_) {
      throw std::bad_array_new_length{};
    }

    storage_.resize(rows * stride_);
  }

  inline T* data() { return storage_.data(); }
  inline const T* data() const { return storage_.data(); }

  inline T* ptr(std::size_t i, std::size_t j) { return storage_.data() + i * stride_ + j; }

  inline const T* ptr(std::size_t i, std::size_t j) const {
    return storage_.data() + i * stride_ + j;
  }

  inline T& operator()(std::size_t i, std::size_t j) { return *ptr(i, j); }

  inline const T& operator()(std::size_t i, std::size_t j) const { return *ptr(i, j); }
};

class GridView;

class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  mutable PaddedAlignedVector<double> grid_;

  // This is an internal storage that it defaults to use the quantized version.
  class Grid_Quantized {
  private:
    static constexpr uint32_t QMAX{std::numeric_limits<uint32_t>::max()};
  
    // My quantization of double to uint32_t is as follows:
    // Within the grid, store the highest and lowest values. This will be the range of which the quantization
    // operates. We also note that since the next grid is a weighted average of its surrounding grid, no 
    // such data would ever leave this range 
    double minimum_{0.0};
    double maximum_{0.0};

    // With the range in mind, we essentially map every value within that range to a respective uint32_t
    // by the formula of y = (range / QMAX) * x + minimum where y is the double and x is the uint32_t
    double scale_{0.0}; // store for QMAX / range 
    double spacing_{0.0}; // store for range / QMAX 

    // Dervived from bench/main.cpp 
    static constexpr double ERROR_BUDGET{1e-6};
    // Initial Error is at most 2.0 since its eitehr than boundary of -unit to +unit of a given
    // quantized value.
    static constexpr double INITIAL_ERROR_UNITS{2.0};
    // The growth is empircally tested. I essentially told AI to generate a bunch of grid
    // of different ranges then keep reducing this error unit per step until reducing it further would
    // cause one of the cases to fail. 
    static constexpr double ERROR_UNITS_PER_STEP{0.04};

    // These are variables to store the error accumulation. 
    double error_bound_{0.0}; // store for the accumulated error 
    double step_error_{0.0}; // store for ERROR_UNITS_PER_STEP * spacing 
    bool initialized_{false}; // store for INTIIAL_ERROR_UNITS * spacing

    PaddedAlignedVector<uint32_t> grid_;

    // friending them since they need to access the quantized values 
    friend class Grid;
    friend class GridView;
    friend void apply_stencil_quantized(const GridView& source, Grid& destination);

  private:
    Grid_Quantized(std::size_t rows, std::size_t cols) : grid_{rows, cols} {}
    
    // This is a helper function in the quantizationm process to update the min and max 
    // within a grid 
    inline void record_write(double new_value) {
      minimum_ = std::min(minimum_, new_value);
      maximum_ = std::max(maximum_, new_value);
    }

    // Takes over all the quantization variables from a soruce, this is the used during the 
    // transfer of the old grid and the next grid  
    inline void inherit_quantization(const Grid_Quantized& source) {
      minimum_ = source.minimum_;
      maximum_ = source.maximum_;
      scale_ = source.scale_;
      spacing_ = source.spacing_;
      error_bound_ = source.error_bound_;
      step_error_ = source.step_error_;
      initialized_ = source.initialized_;
    }

    double minimum() const { return minimum_; }
    double maximum() const { return maximum_; }

    // Unquantize the grid data 
    inline double value(std::size_t row, std::size_t col) const {
      return minimum_ + grid_(row, col) * spacing_;
    }

    inline void write(std::size_t row, std::size_t col, double value){
      grid_(row, col) = static_cast<uint32_t>(scale_ * value - minimum_);
    }

    // Checks whether the quantized grid can continue before needed to switch to unquantized
    bool can_step() const { return error_bound_ + step_error_ <= ERROR_BUDGET; }

    // Intiailize the quantization
    void initialize(const Grid& owner) {
      if (initialized_) {
        return;
      }

      const double range{std::max(maximum_ - minimum_, 1e-20)};

      scale_ = static_cast<double>(QMAX) / range;
      spacing_ = range / static_cast<double>(QMAX);
      error_bound_ += INITIAL_ERROR_UNITS * spacing_;

      // Empirical error estimate, not a worst-case guarantee.
      step_error_ = ERROR_UNITS_PER_STEP * spacing_;

      if (!can_step()) {
        return;
      }

      for (std::size_t row{0}; row < owner.rows_; ++row) {
        for (std::size_t col{0}; col < owner.cols_; ++col) {
          double encoded{(owner.grid_(row, col) - minimum_) * scale_};
          encoded = std::clamp(encoded, 0.0, static_cast<double>(QMAX));
          grid_(row, col) = static_cast<uint32_t>(encoded + 0.5);
        }
      }
      initialized_ = true;
    }

    // Revert the qunatization back to a double by populating the owner grid its original
    // values 
    void decode(const Grid& owner) const {
      for (std::size_t row{0}; row < owner.rows_; ++row) {
        for (std::size_t col{0}; col < owner.cols_; ++col) {
          owner.grid_(row, col) = value(row, col);
        }
      }
    }
  };

  // Mutable since we require to update the internal quantization regardless if it is a 
  // const reference 
  mutable Grid_Quantized quantized_;
  mutable bool use_quantized_{true};

  // This step count will be useful in a specific operation to reduce the error 
  std::size_t step_count_{0};

  // reverts quantization utilization back to double 
  void switch_to_double() const {
    if (!use_quantized_) {
      return;
    }

    if (quantized_.initialized_) {
      quantized_.decode(*this);
    }
    use_quantized_ = false;
  }

  // Call to prep the quantization or revert if the error bound has exceeded 
  void prepare_quantization() const {
    if (!use_quantized_) {
      return;
    }

    quantized_.initialize(*this);
    if (!quantized_.can_step()) {
      switch_to_double();
    }
  }

  // Setting a value to the quantized grid or non quantized grid depending on state 
  void set_value(std::size_t row, std::size_t col, double value) {
    if (!std::isfinite(value)) {
      throw std::invalid_argument("Grid values must be finite");
    }
    
    if (quantized_.initialized_ && use_quantized_){
      // Switch if it breaks the quantization range
      if (quantized_.maximum_ < value || quantized_.minimum_ > value) {
        switch_to_double();
      } else { 
        // Otherwise, simply write the value in it
        quantized_.write(row, col, value);
        return; 
      }
    }
    if (use_quantized_){
      quantized_.record_write(value);
    }
    
    grid_(row, col) = value;
  }

  double get_value(std::size_t row, std::size_t col) const {
    if (use_quantized_ && quantized_.initialized_) {
      return quantized_.value(row, col);
    }

    return grid_(row, col);
  }

  friend class GridView;
  friend void apply_stencil_impl(const GridView& source, Grid& destination);
  friend void apply_stencil_quantized(const GridView& source, Grid& destination);
  friend void apply_stencil(const Grid& old_grid, Grid& new_grid);

public:
  // Proxy that handle whether the quantized value or non-quantized value be shown. 
  class ConstCellProxy {
  private:
    const Grid& owner_;
    std::size_t row_;
    std::size_t col_;

  public:
    ConstCellProxy(const Grid& owner, std::size_t row, std::size_t col)
        : owner_{owner}, row_{row}, col_{col} {}

    operator double() const {
      return owner_.get_value(row_, col_);
    }
  };

  class CellProxy {
  private:
    Grid& owner_;
    std::size_t row_;
    std::size_t col_;

  public:
    CellProxy(Grid& owner, std::size_t row, std::size_t col)
        : owner_{owner}, row_{row}, col_{col} {}

    operator double() const { return static_cast<const Grid&>(owner_)(row_, col_); }

    CellProxy& operator=(double value) {
      owner_.set_value(row_, col_, value);
      return *this;
    }

    CellProxy& operator=(const CellProxy& other) { return *this = static_cast<double>(other); }
  };

  Grid(std::size_t rows, std::size_t cols)
      : rows_{rows}, cols_{cols}, grid_{rows, cols}, quantized_{rows, cols} {
    if (rows == 0 || cols == 0) {
      throw std::invalid_argument("Grid dimensions must be positive");
    }
  }

  inline std::size_t rows() const { return rows_; }
  inline std::size_t cols() const { return cols_; }

  CellProxy operator()(std::size_t i, std::size_t j) { 
    return CellProxy(*this, i, j); 
  }

  ConstCellProxy operator()(std::size_t i, std::size_t j) const {
    return ConstCellProxy(*this, i, j);
  }

private:
  inline double* ptr(std::size_t i, std::size_t j) { return grid_.ptr(i, j); }

  inline const double* ptr(std::size_t i, std::size_t j) const { return grid_.ptr(i, j); }

  inline uint32_t* quantized_ptr(std::size_t i, std::size_t j) {
    return quantized_.grid_.ptr(i, j);
  }

  inline const uint32_t* quantized_ptr(std::size_t i, std::size_t j) const {
    return quantized_.grid_.ptr(i, j);
  }
};

// Borrow either representation without copying or decoding on construction.
class GridView {
private:
  const Grid& grid_;

  const Grid::Grid_Quantized& quantization() const { return grid_.quantized_; }

  friend void apply_stencil_quantized(const GridView& source, Grid& destination);

public:
  GridView(const Grid& grid) : grid_{grid} {}

  inline std::size_t rows() const { return grid_.rows(); }
  inline std::size_t cols() const { return grid_.cols(); }

  bool is_quantized() const { return grid_.use_quantized_ && grid_.quantized_.initialized_; }

  double operator()(std::size_t i, std::size_t j) const { return grid_(i, j); }

  // Raw double storage; values may be stale while quantized.
  inline const double* ptr(std::size_t i, std::size_t j) const { return grid_.ptr(i, j); }

  inline const uint32_t* quantized_ptr(std::size_t i, std::size_t j) const {
    return grid_.quantized_ptr(i, j);
  }
};

// Quantized value implementation. We use the remainder and rounding offset to prevent the error 
// to have a continued downward shift.
inline uint32_t quantized_stencil_value(uint32_t center, uint32_t top, uint32_t bottom,
                                       uint32_t left, uint32_t right, uint32_t rounding_offset) {
  
  // We divide before adding to prevent overflow 
  const uint32_t whole{center / 2u + top / 8u + bottom / 8u + left / 8u + right / 8u};
  const uint32_t remainder{4u * (center % 2u) + (top + bottom + left + right) % 8u};

  return whole + (remainder + rounding_offset) / 8u;
}

// Stencil implementation for the quantized case
inline void apply_stencil_quantized(const GridView& source, Grid& destination) {
  const std::size_t rows{source.rows()};
  const std::size_t cols{source.cols()};
  const auto& old{source.quantization()};
  
  // This is what the step count is for. In the quantized stencil calculate. We divide the remainder by 8, so 
  // if the remainder of (remainder + rounding_offset) / 8 is 4 then sometimes we round up, and sometimes we round down. 
  // This decrease the error rate since at remainder 4 our alternating representation will not be lost. 
  const uint32_t rounding_offset{3u + static_cast<uint32_t>(source.grid_.step_count_% 2)};

  // inherit next quantization and increase error bound 
  auto& next{destination.quantized_};
  next.inherit_quantization(old);
  next.error_bound_ += next.step_error_;

  // standard stencil implementation.
  std::memcpy(destination.quantized_ptr(0, 0), source.quantized_ptr(0, 0), cols * sizeof(uint32_t));
  std::memcpy(destination.quantized_ptr(rows - 1, 0), source.quantized_ptr(rows - 1, 0),cols * sizeof(uint32_t));

  #pragma omp parallel for schedule(static)
  for (std::size_t row = 1; row < rows - 1; ++row) {
    const uint32_t* top{source.quantized_ptr(row - 1, 0)};
    const uint32_t* mid{source.quantized_ptr(row, 0)};
    const uint32_t* bottom{source.quantized_ptr(row + 1, 0)};
    uint32_t* out{destination.quantized_ptr(row, 0)};

    out[0] = mid[0];
    out[cols - 1] = mid[cols - 1];

    #pragma omp simd
    for (std::size_t col = 1; col < cols - 1; ++col) {
      out[col] = quantized_stencil_value(mid[col], top[col], bottom[col], mid[col - 1],
                                         mid[col + 1], rounding_offset);
    }
  }
}

// Regular stencil implementation.
inline void apply_stencil_impl(const GridView& old_grid, Grid& new_grid) {
  const std::size_t rows{old_grid.rows()};
  const std::size_t cols{old_grid.cols()};

  if (!(rows == new_grid.rows() && cols == new_grid.cols())) {
    throw std::invalid_argument("Input and output grids must have matching dimensions");
  }

  std::memcpy(new_grid.ptr(0, 0), old_grid.ptr(0, 0), cols * sizeof(double));
  std::memcpy(new_grid.ptr(rows - 1, 0), old_grid.ptr(rows - 1, 0), cols * sizeof(double));

  #pragma omp parallel for schedule(static)
  for (std::size_t i = 1; i < rows - 1; ++i) {
    const double* top{old_grid.ptr(i - 1, 0)};
    const double* mid{old_grid.ptr(i, 0)};
    const double* bottom{old_grid.ptr(i + 1, 0)};

    double* __restrict out{new_grid.ptr(i, 0)};

    out[0] = mid[0];
    out[cols - 1] = mid[cols - 1];

    #pragma omp simd
    for (std::size_t j = 1; j < cols - 1; ++j) {
      out[j] = 0.5 * mid[j] + 0.125 * (top[j] + bottom[j] + mid[j - 1] + mid[j + 1]);
    }
  }
}

// this is the entry point and it determines whether we use the quantized or regular version 
inline void apply_stencil(const Grid& old_grid, Grid& new_grid) {
  if (old_grid.rows() != new_grid.rows() || old_grid.cols() != new_grid.cols()) {
    throw std::invalid_argument("Input and output grids must have matching dimensions");
  }

  // Calls the prepare quantization so it would handle the case appropriately of a never quantized grid 
  // being quantized, quantized grid will retain itself, or quantize grid exceeding error can switch
  // to not using quantization 
  old_grid.prepare_quantization();
  if (!new_grid.use_quantized_) {
    old_grid.switch_to_double();
  }

  const GridView source{old_grid};

  if (source.is_quantized()) {
    apply_stencil_quantized(source, new_grid);
  } else {
    new_grid.use_quantized_ = false;
    apply_stencil_impl(source, new_grid);
  }

  new_grid.step_count_ = old_grid.step_count_ + 1;
}