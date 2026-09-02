#pragma once

#include <cstddef>
#include <vector>
#include <thread>
#include <atomic>
#include <immintrin.h>

#include <cstring>

// method to process calculations for a group of rows
inline void process_rows(const double *__restrict__ old_base,
                         double *__restrict__ new_base,
                         std::size_t begin_row, std::size_t end_row,
                         std::size_t cols)
{
  for (std::size_t i = begin_row; i < end_row; ++i)
  {
    const double *curr = old_base + i * cols;
    const double *prev = old_base + (i - 1) * cols;
    const double *next = old_base + (i + 1) * cols;
    double *new_cell = new_base + i * cols;

    // copy boundary cells for row
    new_cell[0] = curr[0];
    new_cell[cols - 1] = curr[cols - 1];

    for (std::size_t j = 1; j < cols - 1; ++j)
    {
      new_cell[j] = 0.5 * curr[j] + 0.125 * (curr[j + 1] + curr[j - 1] + prev[j] + next[j]);
    }
  }
}

// Persistent thread pool with workers spinning on gen_ while waiting for next stencil iterations
class ThreadPool
{
private:
  std::vector<std::thread> workers_;
  std::size_t worker_count_ = 0;

  const double *old_base_ = nullptr;
  double *new_base_ = nullptr;

  std::size_t rows_ = 0;
  std::size_t cols_ = 0;

  // alignas to isolate hot sync cache lines
  alignas(64) std::atomic<std::size_t> gen_{0};
  alignas(64) std::atomic<bool> stop_{false};
  alignas(64) std::atomic<std::size_t> completed_{0};

  // for load balancing, the percentage of interior rows that main thread is handling
  static constexpr std::size_t main_percent_ = 25;

  // main loop for threads
  void worker_loop(std::size_t worker_id)
  {
    std::size_t seen_gen = 0;

    while (true)
    {
      // spin waiting
      std::size_t current_gen;
      while (true)
      {
        if (stop_.load(std::memory_order_acquire))
        {
          return;
        }

        // acquire pairs with run() updating gen_ and publishing job metadata
        current_gen = gen_.load(std::memory_order_acquire);

        if (current_gen != seen_gen)
        {
          break;
        }

        _mm_pause();
      }

      const double *old_base = old_base_;
      double *new_base = new_base_;
      const std::size_t rows = rows_;
      const std::size_t cols = cols_;

      seen_gen = current_gen;

      // partitioning the interior rows for workers
      const std::size_t interior_rows = rows - 2;
      const std::size_t main_rows = interior_rows * main_percent_ / 100;
      const std::size_t worker_rows = interior_rows - main_rows;
      const std::size_t begin_row = 1 + worker_rows * worker_id / (worker_count_);
      const std::size_t end_row = 1 + worker_rows * (worker_id + 1) / (worker_count_);

      // actually process the rows
      process_rows(old_base, new_base, begin_row, end_row, cols);

      // signal completion by adding to the count of completed workers
      completed_.fetch_add(1, std::memory_order_acq_rel);
    }
  }

public:
  ThreadPool()
  {
    // empirically tested to be fastest in test environment
    worker_count_ = 3;
    workers_.reserve(worker_count_);

    for (std::size_t i = 0; i < worker_count_; ++i)
    {
      workers_.emplace_back([this, i]
                            { worker_loop(i); });
    }
  }

  ~ThreadPool()
  {
    stop_.store(true, std::memory_order_release);

    for (std::thread &worker : workers_)
    {
      if (worker.joinable())
      {
        worker.join();
      }
    }
  }

  void run(const double *old_base, double *new_base, std::size_t rows, std::size_t cols)
  {
    // publish job data before incrementing gen_ and causing workers to process new job
    old_base_ = old_base;
    new_base_ = new_base;
    rows_ = rows;
    cols_ = cols;

    completed_.store(0, std::memory_order_relaxed);

    gen_.fetch_add(1, std::memory_order_release);

    // cv_.notify_all();

    // partitioning the interior rows for main thread
    const std::size_t interior_rows = rows - 2;
    const std::size_t main_rows = interior_rows * main_percent_ / 100;
    const std::size_t worker_rows = interior_rows - main_rows;
    const std::size_t begin_row = worker_rows + 1;
    const std::size_t end_row = rows - 1;

    // main thread handles portion of row iterations
    process_rows(old_base, new_base, begin_row, end_row, cols);

    // main thread copies over top and bottom boundary rows while workers may still be computing
    std::memcpy(new_base, old_base, cols * sizeof(double));
    std::memcpy(new_base + (rows - 1) * cols, old_base + (rows - 1) * cols, cols * sizeof(double));

    // wait for all workers to complete tasks
    while (completed_.load(std::memory_order_acquire) != worker_count_)
    {
      _mm_pause();
    }
  }
};

// keep thread pool between stencil iterations
inline ThreadPool &get_thread_pool()
{
  static ThreadPool pool;
  return pool;
}

class Grid
{
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> data;

public:
  Grid(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols), data(rows * cols)
  {
    // construct the pool during Grid construction rather than the timed stencil loop
    (void)get_thread_pool();
  }

  double &operator()(std::size_t i, std::size_t j)
  {
    return data[i * cols_ + j];
  }

  double operator()(std::size_t i, std::size_t j) const
  {
    return data[i * cols_ + j];
  }

  std::size_t rows() const
  {
    return rows_;
  }

  std::size_t cols() const
  {
    return cols_;
  }

  const double *base() const
  {
    return data.data();
  }

  double *base()
  {
    return data.data();
  }
};

void apply_stencil(const Grid &old_grid, Grid &new_grid)
{
  const double *__restrict__ old_base = old_grid.base();
  double *__restrict__ new_base = new_grid.base();
  const std::size_t rows = old_grid.rows();
  const std::size_t cols = old_grid.cols();

  // handle small grids where all cells are boundary
  if (rows < 3 || cols < 3)
  {
    for (std::size_t i = 0; i < rows; ++i)
    {
      for (std::size_t j = 0; j < cols; ++j)
      {
        new_grid(i, j) = old_grid(i, j);
      }
    }

    return;
  }

  get_thread_pool().run(old_base, new_base, rows, cols);
}