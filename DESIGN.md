# 2D Heat Diffusion Solver

# Overview

The 2D heat diffusion solver consists of two components:

1. A grid class that stores the 2D temperature field in memory.
2. The stencil kernel that advances the simulation by one time step.

## Governing heat equation

Heat spreads across a 2D surface according to the following PDE:

$$
\frac{\partial T}{\partial t} = \alpha(\frac{\partial^2 T}{\partial x^2} + \frac{\partial^2 T}{\partial y^2})
$$

where the function $T(x,y,t)$ denotes the temperature of a point $(x,y)$ on the surface at time $t$, and $\alpha$ is the thermal diffusivity constant.

## Discretization onto a grid

Using the forward-time, centered-space (FTCS) method, we replace the continuous derivatives with finite differences and solve for the future value $T_{t+1}(i,j)$. This yields the following:

$$
T_{t+1}(i,j) = T_{t}(i,j) + \Delta t \cdot \alpha (\frac{T_t(i+1,j) - 2T_{t}(i,j) + T_{t}(i-1,j)}{\Delta x^2} + \frac{T_t(i,j+1) - 2T_{t}(i,j) + T_{t}(i,j-1)}{\Delta y^2})
$$

We assume equal spatial step sizes, i.e., $\Delta y = \Delta x$. This does not require equal numbers of rows and columns. Choosing $\alpha \Delta t / \Delta x^2 = 0.125$ within the FTCS stability limit of $0.25$ for this 2D grid yields the following:

$$
T_{t+1}(i,j) = 0.5 \cdot T_t(i,j) + 0.125 \cdot (T_t(i-1, j) + T_t(i+1,j) + T_t(i,j-1) + T_t(i,j+1))
$$

# Grid class implementation

We implement the 2D grid data container using `std::vector<double>`, and store the grid in row-major order so adjacent columns occupy adjacent memory locations. We guarantee that grid rows never straddle cache lines.

The implementation introduces a `stride_` member and maps a grid coordinate $(i,j)$ to `data_[i * stride_ + j]`. Note that `cols_` is the number of logical columns, while `stride_` is the number of elements reserved for each row, including padding.

>We assume a cache line size of 64 bytes and an 8-byte `double`. Thus, one cache line holds eight elements. We round the stride up to a multiple of eight.

We implement `GridDataAllocator` and use `std::vector<double, GridDataAllocator<double>>` for the container. Its `allocate` method calls `std::aligned_alloc` with an alignment of 64, rounding the requested byte count up to a multiple of 64. Allocation failure throws `std::bad_alloc`, and `deallocate` releases the memory via `std::free`.

# Stencil kernel implementation

We implement the equation for $T_{t+1}(i,j)$ as a five-point stencil kernel in the simulation program, and use the following OpenMP directives:

```cpp
#pragma omp parallel for schedule(static)
for (std::size_t i = 1; i < rows - 1; ++i) {
  new_grid(i, 0) = old_grid(i, 0);
  new_grid(i, cols - 1) = old_grid(i, cols - 1);

  const double *center_row = old_grid.row_view(i);
  const double *above_row = old_grid.row_view(i - 1);
  const double *below_row = old_grid.row_view(i + 1);
  double *__restrict out_row = new_grid.row_view_mutable(i);
#pragma omp simd aligned(center_row, above_row, below_row, out_row : 64)
  for (std::size_t j = 1; j < cols - 1; ++j) {
    out_row[j] =
        0.5 * center_row[j] + 0.125 * (above_row[j] + below_row[j] +
                                    center_row[j - 1] + center_row[j + 1]);
  }
}
```

`omp parallel for schedule(static)` splits the row-wise iterations into approximately equal contiguous chunks, assigning at most one chunk to each thread. This gives us a balanced assignment without repeatedly requesting work from a dynamic scheduler. Threads may read overlapping regions of the old grid, since that buffer is unchanged during the call.

It follows that the column loop also has independent output iterations. Our flat vector places these outputs next to each other in memory, making SIMD feasible. The overlapping reads of neighbouring temperatures do not introduce a dependency between outputs.

>The `aligned` clause explicitly informs the compiler that the four row pointers are 64-byte aligned. This is a property ensured by our allocator and stride.

# Benchmarking the implementations

## Benchmarking program

The benchmark uses a $1024 \times 1024$ grid and advances the simulation by 200 time steps. It also times an independent reference implementation on the same problem, storing its grid as a `std::vector<std::vector<double>>`. The reported score is `reference_ms / runtime_ms`, so a score greater than one means the actual implementation is faster than the reference in that run.

## Locally recorded results

The local benchmark yields the following averages recorded during development. The changes are cumulative.

| Implementation stage                                     | Runtime (ms) | Reported memory (MB) | Score | Runtime reduction from preceding stage |
| -------------------------------------------------------- | -----------: | -------------------: | ----: | -------------------------------------: |
| Flat vector baseline                                     |       57.464 |               16.777 | 0.793 |                                      — |
| OpenMP row parallelism and column SIMD                   |       23.724 |               16.777 | 2.009 |                                  58.7% |
| Padded stride and row pointers, including `__restrict`   |       22.270 |               16.777 | 2.171 |                                   6.1% |
| Aligned allocator, following the boundary-copying change |       21.141 |               16.777 | 2.250 |                                   5.1% |

## Flat vector baseline

We begin with a flat vector and a serial stencil traversal. This gives us contiguous storage and a simple indexing scheme. The runtime of 57.464 ms and score of 0.793 show that this implementation is slower than the harness reference.

> While not outstanding, this gives us a baseline to compare subsequent implementations against.

## Row parallelism and SIMD

Adding OpenMP row parallelism and column SIMD reduces runtime to 23.724 ms. This is approximately a 58.7% reduction in runtime.

The two-buffer arrangement provides the independence needed for both changes. The row loop distributes work among threads, while the contiguous column loop allows each thread to compute multiple outputs with SIMD. Since both were introduced together, the measurement establishes their combined benefit.

> This is a clear improvement.

## Padded stride and row pointers

The next change introduces the padded stride and row pointers, reducing runtime to 22.270 ms, approximately 6.1% below the preceding stage. Row pointers express the inner loop as offsets from four row starts, avoiding repeated 2D indexing in the source.

We also investigate pointer aliasing. The output row uses `__restrict` to express that the locations written through this pointer are not accessed through unrelated pointers in this scope. This is valid because the output buffer is separate from the input buffer. It gives the compiler information that may help it avoid alias checks or conservative code generation.

# Areas for improvement

## Cache set conflicts

Alignment does not by itself ensure that accesses are distributed evenly across cache sets.

Assume a 32 KiB, 8-way set-associative L1D cache with 64-byte cache lines and a cache set index calculated as the cache line number modulo the number of sets. This gives us:

$$
\text{cache lines} = \frac{32 \cdot 1024}{64} = 512, \qquad
\text{cache sets} = \frac{512}{8} = 64
$$

For a stride of 1024 doubles, elements in the same column of consecutive rows are 8192 bytes apart. This is 128 cache lines, and $128 \bmod 64 = 0$. Thus, these elements map to the same cache set under this model.

This is a possible source of conflict misses. One experiment to prove this is to add an extra cache line of padding, giving a stride of 1032 doubles for this grid. Row starts would remain 64-byte aligned, but would be separated by 129 cache lines. Since $129 \bmod 64 = 1$, successive rows would advance through the cache sets. We would need runtime measurements and cache-miss counters on the target CPU to establish whether this helps.
