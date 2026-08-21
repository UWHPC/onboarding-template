#pragma once

#include <cstddef>
#include <vector>
#include <new>
#include <utility>
// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.

/*
Notes:
- Seems like we're simulating how heat spreads accross a heat conductive surface
- We represent this surface as a grid in the grid class
- It seems like the outer edges of the grid are permanently at their initial temp
- Grid class holds all temp values
- apply stencil calculates new temp
- we have 2 grids old grid and new grid
- we read each value on old grid then using the stencil function
- calculate new values and put in new grid
- i need a data structure to actually hold the grid in, probably a vector
*/

/*
Notes after Comments:
- SIMD: Single Instruction multiple data, from what i understand is pretty much cpu
- being able to do one instruction with multiple data at one time, which makes sense to be
- use full for our five point stencil.
- Memory allignment is needed now because in our vector, the vector library or whatever
- guarantees it is aligned for 8 bytes, meanign from wherever it starts the next address would
- be divisible by 8. Bu if we want single instruction multiple data we'd need to ensure that it's
- alligned to more than 8 bytes so it can use multiple doubles in one instruction.
- So we can run each of the rows in parallel with multiple thread then compute each column
- quicker by using SIMD.
*/

// so how we are going about memory alignment here is that the grid owns a vector
// vector asks its allocatoe to give memory for doubles and the allocator asks cpp runtime for memory
// starting on a 64-byte boundary. From my research x86 cpus when they read from main memory they bring
// a 64 byte block into cache too. Meaning bringing 7 other nearby doubles. All to say that the data is
// pretty much just laid out ore neatly.
// ok so I'm using template here as everything i found online and AI is saying to but I do understand
// that it's typically used for when we reuse functions and stuff to work with different data types, even
// though here we only using for doubles.
// ::operator new() reserves raw memory which std::vector uses to construct/store doubles
// by using view we can make it so that the stencil function does not need to own the whole grid object considering
// its only using the simple values for calculations
template <typename T, std::size_t Alignment>
class AlignedAllocator
{
public:
  using value_type = T;
  template <typename U>
  // compiler says rebind is needed, im assuming to account for allocating different data types?
  struct rebind
  {
    using other = AlignedAllocator<U, Alignment>;
  };
  AlignedAllocator() noexcept = default;
  // give vector a pointer to enough raw memory for count values of Type T
  // where count is the number of doubles requested
  T *allocate(std::size_t count)
  {
    return static_cast<T *>(               // static cast says that the memory will be used by vector for doubles
        ::operator new(                    // memory-reservation function returns void pointer to reserved address in memory
            count * sizeof(T),             // how many bytes
            std::align_val_t{Alignment})); // make starting addr multiple of this alignment
  }
  void deallocate(T *pointer, std::size_t) noexcept
  {
    ::operator delete(
        pointer,                    // The memory block to release.
        std::align_val_t{Alignment} // Must match the allocation alignment.
    );
  }
};

struct GridView
{
  const double *cells;
  std::size_t rows;
  std::size_t cols;
  std::size_t stride;
};

// A writable borrowed view of grid storage.
// It does not own the memory it points to.
struct MutableGridView
{
  double *cells;
  std::size_t rows;
  std::size_t cols;
  std::size_t stride;
};
class Grid
{
private:
  // pretty much says alignment value is always 64
  static constexpr std::size_t kAlignment{64};
  // calculates how many doubles fit in 64 bytes, is reusable in the case i dont want to use 64
  static constexpr std::size_t kDoublesPerAlignedRow{
      kAlignment / sizeof(double)};
  std::size_t rows_;
  std::size_t cols_;
  std::size_t stride_; // includes doubles that are physically reserved in memory for a specific row, including padding
  std::vector<double, AlignedAllocator<double, kAlignment>> cells_;

public:
  // using an intializer list
  // searched up and found this, it is very slightly quicker as it makes the real zero-filled
  // vector immediatley instead of making one then copying zeros everywhere
  Grid(std::size_t rows, std::size_t cols)
      : rows_{rows}, cols_{cols},
        stride_{// rounds column count up to a multiple of 8
                ((cols + kDoublesPerAlignedRow - 1) / kDoublesPerAlignedRow) * kDoublesPerAlignedRow}

        ,
        cells_(rows * stride_, 0.0)
  {
  }
  // allows us to assign temperature to a square
  // returns direct access (&)to stored cell so it can be written to
  double &operator()(std::size_t i, std::size_t j)
  {
    return cells_[i * stride_ + j];
  }
  // read value at square on grid
  double operator()(std::size_t i, std::size_t j) const
  {
    return cells_[i * stride_ + j];
  }

  GridView view() const
  {
    return {cells_.data(), rows_, cols_, stride_};
  }

  // Called on a writable Grid, such as new_grid.
  MutableGridView view()
  {
    return {cells_.data(), rows_, cols_, stride_};
  }
};

// Do the calculation in a helper:
constexpr double five_point_stencil(
    double center,
    double above,
    double below,
    double left,
    double right)
{
  return 0.5 * center +
         0.125 * (above + below + left + right);
}
void apply_stencil_view(
    const GridView old,
    MutableGridView next)
{
  if (old.rows == 0 || old.cols == 0)
  {
    return;
  }

  // The views are tiny; copying them copies only pointers/sizes, not grid data.
  const std::size_t rows{old.rows};
  const std::size_t cols{old.cols};
  const std::size_t stride{old.stride};

  // Promise that input and output storage does not overlap.
  const double *__restrict old_cells{old.cells};
  double *__restrict next_cells{next.cells};

  // Copy top and bottom boundaries.
  const std::size_t bottom_row{(rows - 1) * stride};

  for (std::size_t j = 0; j < cols; ++j)
  {
    next_cells[j] = old_cells[j];
    next_cells[bottom_row + j] = old_cells[bottom_row + j];
  }

  // Copy left and right boundaries.
  for (std::size_t i = 1; i < rows - 1; ++i)
  {
    const std::size_t row_start{i * stride};

    next_cells[row_start] = old_cells[row_start];
    next_cells[row_start + cols - 1] =
        old_cells[row_start + cols - 1];
  }

// The fast interior calculation works only with views/raw storage.
#pragma omp parallel for
  for (std::size_t i = 1; i < rows - 1; ++i)
  {
    const std::size_t row_start{i * stride};

#pragma omp simd
    for (std::size_t j = 1; j < cols - 1; ++j)
    {
      const std::size_t index{row_start + j};

      next_cells[index] = five_point_stencil(
          old_cells[index],
          old_cells[index - stride],
          old_cells[index + stride],
          old_cells[index - 1],
          old_cells[index + 1]);
    }
  }
}

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid &old_grid, Grid &new_grid)
{
  apply_stencil_view(old_grid.view(), new_grid.view());
}
/*
Without Parallel:
cmake --build --preset benchmark
ctest --preset benchmark --output-on-failure
./build/benchmark/uwhpc_benchmark

With Parallel:
ctest --test-dir build/benchmark-omp --output-on-failure
./build/benchmark-omp/uwhpc_benchmark
*/
