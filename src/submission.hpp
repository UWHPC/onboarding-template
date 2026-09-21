#pragma once

#include <cstddef>
#include <vector>
#include <thread>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <omp.h>

struct Worker_Intervals {
  std::size_t start_row;
  std::size_t end_row;
};

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.

class Grid {
private:

std::size_t cols_;
std::size_t rows_;

std::size_t active_workers_;
std::size_t interior_rows_;
std::size_t base_interval_;
std::size_t extra;

bool first_iteration_ = true;

// Represent 2D values as a flat 1D vector
std::vector<double> temps_;

// Threads can safely read from this simultaneously once set by Grid's constructor with no mutex
std::vector<Worker_Intervals> intervals_;


public:
  Grid(std::size_t rows, std::size_t cols);

  double& operator()(std::size_t i, std::size_t j);
  double  operator()(std::size_t i, std::size_t j) const;

  std::size_t get_rows() const { return rows_; }
  std::size_t get_cols() const { return cols_; }
  std::size_t get_interior_rows() const { return interior_rows_; }
  std::size_t get_active_workers() const { return active_workers_; }

  bool first_iteration() const { return first_iteration_; }
  void set_first_iteration_false() { first_iteration_ = false; }

  std::size_t get_start_row(std::size_t worker_id) const {
    return intervals_[worker_id].start_row;
  }
  std::size_t get_end_rows(std::size_t worker_id) const {
    return intervals_[worker_id].end_row;
  }

};

void update_grid(std::size_t start_row, std::size_t end_row, const Grid& old_grid, Grid& new_grid) {
  for (std::size_t i = start_row; i < end_row; ++i) {

        #pragma omp simd
        for (std::size_t j = 1; j < old_grid.get_cols() - 1; ++j) {
            new_grid(i, j) =
                0.5 * old_grid(i, j) +
                0.125 * (
                    old_grid(i - 1, j) +
                    old_grid(i + 1, j) +
                    old_grid(i, j - 1) +
                    old_grid(i, j + 1)
                );
        }
    }
}

class ThreadPool {
  private:
    std::vector<std::thread> workers_;
    std::size_t active_workers_ = 0;
    std::size_t num_threads_ = 0;

    std::mutex mutex_;
    std::condition_variable wake_cv_;
    std::condition_variable done_cv_;

    bool stop_ = false;

    std::atomic<std::size_t> iteration_{0};
    std::atomic<std::size_t> finished_workers_{0};

    const Grid* old_grid_ = nullptr;
    Grid* new_grid_ = nullptr;

  public:
    ThreadPool(std::size_t num_threads);
    ~ThreadPool();
    void activate_worker(std::size_t worker_id);
    void start_iteration(const Grid& old_grid, Grid& new_grid);
    void wait_for_workers();
};

std::size_t get_active_threads(std::size_t interior_rows) {
  return 8; //hardcoded for now 
  return std::max(
        std::size_t{1},
        std::min(
            static_cast<std::size_t>(std::thread::hardware_concurrency()),
            (interior_rows + 63) / 64)
    );
}

bool should_thread(std::size_t interior_rows, std::size_t cols) {
    return interior_rows > 100; // simple decision for now
}

ThreadPool::ThreadPool(std::size_t num_threads) : num_threads_(num_threads) {
  workers_.resize(num_threads_);

  for (std::size_t t = 0; t < num_threads_; t++) {
    workers_[t] = std::thread(
        &ThreadPool::activate_worker,
        this,
        t
    );
  }
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  } // Unlocks

  // Wake workers that are sleeping on cv_
  wake_cv_.notify_all();

  // Wait for every thread to terminate
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

void ThreadPool::activate_worker(std::size_t worker_id) {
  std::size_t my_iteration = 0;

  while (true) {

    // Automatic unlocking of mutex when lock goes out of scope
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Wakes up from notify_all(),
    // Checks if there is work to be done (or destructor is called),
    // lock if needed.

    // If new work: 
    //   update my_iteration and grids, unlock, do expensive calculation concurrently

    // If no new work (worker finished one iteration, others are still working): 
    //   Don't start a new calculation again, go to sleep, unlock, and wait for notify_all() again.
    //   Needs the mutex because it is possible to see no work to do,
    //   but before going to sleep, apply_stencil might have started a new iteration,
    //   and then sleeps, missing an iteration.
    wake_cv_.wait(lock, [&] {
      return iteration_.load(std::memory_order_acquire) > my_iteration || stop_;
    });

    if (stop_)
        break;
    
    lock.unlock();

    my_iteration = iteration_.load(std::memory_order_acquire);

    // This worker is not needed for this iteration.
    if (worker_id >= active_workers_)
      continue;

    const Grid* old = old_grid_;
    Grid* next = new_grid_;

    update_grid(old->get_start_row(worker_id), old->get_end_rows(worker_id), *old, *next);

    if (finished_workers_.fetch_add(1, std::memory_order_relaxed) + 1 == active_workers_)
      done_cv_.notify_one();
  }
}

void ThreadPool::start_iteration(const Grid& old_grid, Grid& new_grid) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      old_grid_ = &old_grid;
      new_grid_ = &new_grid;
      active_workers_ = old_grid.get_active_workers();
      
      finished_workers_.store(0, std::memory_order_relaxed);
      iteration_.fetch_add(1, std::memory_order_release);
    }

  // Wake all persistent workers
  wake_cv_.notify_all();
}

void ThreadPool::wait_for_workers(){
  std::unique_lock<std::mutex> lock(mutex_);

  done_cv_.wait(lock, [&] {
    return finished_workers_.load(std::memory_order_acquire) == active_workers_;
  });
}

int additional_row(std::size_t& extra_rows) {
  if (extra_rows > 0) {
    extra_rows--;
    return 1;
  }
  return 0;
}

void update_grid(
    std::size_t start_row,
    std::size_t end_row,
    const Grid& old_grid,
    Grid& new_grid
);

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid){

  if (new_grid.first_iteration()) {
    // Top and bottom rows
    for (std::size_t j = 0; j < old_grid.get_cols(); ++j) {
    new_grid(0, j) = old_grid(0, j);
    new_grid(old_grid.get_rows() - 1, j) =
        old_grid(old_grid.get_rows() - 1, j);
    }

    // Left and right columns
    for (std::size_t i = 1; i < old_grid.get_rows() - 1; ++i) {
      new_grid(i, 0) = old_grid(i, 0);
      new_grid(i, old_grid.get_cols() - 1) =
        old_grid(i, old_grid.get_cols() - 1);
    }

    new_grid.set_first_iteration_false();
  }

  if (should_thread(old_grid.get_interior_rows(), old_grid.get_cols())) {
    static ThreadPool thread_pool(8);
   
    thread_pool.start_iteration(old_grid, new_grid);
    thread_pool.wait_for_workers();
  } else {
    update_grid(1, old_grid.get_rows() - 1, old_grid, new_grid);
  }

}

Grid::Grid(std::size_t rows, std::size_t cols)
    : rows_(rows), cols_(cols), temps_(rows * cols, 0.0), interior_rows_(rows > 2 ? rows - 2 : 0) {

      active_workers_ = get_active_threads(interior_rows_);

      base_interval_ = interior_rows_ / active_workers_;
      extra = interior_rows_ % active_workers_;

      intervals_.resize(active_workers_);

      std::size_t current_row = 1;

      for (std::size_t t = 0; t < active_workers_; ++t) {

        std::size_t rows_for_thread = base_interval_ + additional_row(extra);
        intervals_[t].start_row = current_row;
        intervals_[t].end_row = intervals_[t].start_row + rows_for_thread;

        current_row = intervals_[t].end_row;

      }

     }

double& Grid::operator()(std::size_t i, std::size_t j) {
  return temps_[i * cols_ + j];
}

double Grid::operator()(std::size_t i, std::size_t j) const {
  return temps_[i * cols_ + j];
}