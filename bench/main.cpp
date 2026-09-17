// Evaluation harness (provided; do not edit).
//
// Runs the PUBLIC correctness cases and the benchmark. The real evaluator runs
// these same public cases plus a set of private cases you cannot see here, so a
// submission that passes locally can still fail on hidden cases.
//
//   --check     run the public correctness cases; exit non-zero on any failure
//   (no args)   run the benchmark; print JSON to stdout
//
// Score is reference_ms / your_ms on the benchmark case; higher is faster. The
// reference below is deliberately naive, so it is a floor to clear rather than
// a design to imitate.

#include "submission.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::high_resolution_clock;

constexpr double kTolerance{1e-6};

enum class InitialCondition { kCenterBlock, kLinearGradient, kCheckerboard };

double initial_temperature(
  const InitialCondition condition,
  const std::size_t rows, const std::size_t cols,
  const std::size_t row, const std::size_t col
) {
  switch (condition) {
    case InitialCondition::kCenterBlock: {
      const std::size_t block_row_begin{rows / 2 - rows / 8};
      const std::size_t block_row_end{rows / 2 + rows / 8};
      const std::size_t block_col_begin{cols / 2 - cols / 8};
      const std::size_t block_col_end{cols / 2 + cols / 8};

      const bool inside_block{
        row >= block_row_begin && row < block_row_end &&
        col >= block_col_begin && col < block_col_end
      };

      return inside_block ? 100.0 : 0.0;
    }
    case InitialCondition::kLinearGradient: {
      return static_cast<double>(row + col);
    }
    case InitialCondition::kCheckerboard: {
      return ((row + col) % 2 == 0) ? 1.0 : 0.0;
    }
  }
  return 0.0;
}

// Independent reference implementation.
struct ReferenceGrid {
  std::size_t rows;
  std::size_t cols;
  std::vector<std::vector<double>> cells;

  ReferenceGrid(const std::size_t row_count, const std::size_t column_count)
    : rows{row_count}
    , cols{column_count}
    , cells(row_count, std::vector<double>(column_count, 0.0))
  { }

  double& at(const std::size_t row, const std::size_t col)       { return cells[row][col]; }
  double  at(const std::size_t row, const std::size_t col) const { return cells[row][col]; }
};

void apply_reference_stencil(const ReferenceGrid& old_grid, ReferenceGrid& new_grid) {
  const std::size_t rows{old_grid.rows};
  const std::size_t cols{old_grid.cols};

  for (std::size_t row{}; row < rows; ++row) {
    new_grid.at(row, 0) = old_grid.at(row, 0);
    new_grid.at(row, cols - 1) = old_grid.at(row, cols - 1);
  }

  for (std::size_t col{}; col < cols; ++col) {
    new_grid.at(0, col) = old_grid.at(0, col);
    new_grid.at(rows - 1, col) = old_grid.at(rows - 1, col);
  }

  for (std::size_t row{1}; row < rows - 1; ++row) {
    for (std::size_t col{1}; col < cols - 1; ++col) {
      new_grid.at(row, col) = 0.5   * old_grid.at(row, col) +
                              0.125 * (old_grid.at(row - 1, col) + old_grid.at(row + 1, col) +
                                       old_grid.at(row, col - 1) + old_grid.at(row, col + 1));
    }
  }
}

struct CorrectnessCase {
  std::size_t rows;
  std::size_t cols;
  std::size_t steps;
  InitialCondition condition;
  const char* name;
};

// The public cases. The evaluator runs these plus additional private cases.
const CorrectnessCase kPublicCases[]{
  {32, 32, 20, InitialCondition::kCenterBlock,    "public/square-32"},
  {48, 80, 40, InitialCondition::kLinearGradient, "public/nonsquare-48x80"},
  {64, 64, 30, InitialCondition::kCheckerboard,   "public/checker-64"},
  {50, 50, 1,  InitialCondition::kCenterBlock,    "public/one-step-50"},
  {50, 80, 10, InitialCondition::kLinearGradient, "public/nonsquare-boundary-50x80"},
};

double max_difference_from_reference(
  const std::size_t rows, const std::size_t cols,
  const std::size_t steps, const InitialCondition condition
) {
  // Ping-pong storage. The pointers below carry the old/new roles and swap every
  // step, so these four objects are just buffers.
  Grid submission_first{rows, cols};
  Grid submission_second{rows, cols};
  ReferenceGrid reference_first{rows, cols};
  ReferenceGrid reference_second{rows, cols};

  for (std::size_t row{}; row < rows; ++row) {
    for (std::size_t col{}; col < cols; ++col) {
      const double temperature{initial_temperature(condition, rows, cols, row, col)};

      submission_first(row, col)   = temperature;
      reference_first.at(row, col) = temperature;
    }
  }

  Grid* submission_old{&submission_first};
  Grid* submission_new{&submission_second};
  ReferenceGrid* reference_old{&reference_first};
  ReferenceGrid* reference_new{&reference_second};

  for (std::size_t step{}; step < steps; ++step) {
    apply_stencil(*submission_old, *submission_new);
    std::swap(submission_old, submission_new);

    apply_reference_stencil(*reference_old, *reference_new);
    std::swap(reference_old, reference_new);
  }

  const Grid& submission_result{*submission_old};
  const ReferenceGrid& reference_result{*reference_old};

  double max_difference{};

  for (std::size_t row{}; row < rows; ++row) {
    for (std::size_t col{}; col < cols; ++col) {
      const double difference{
        std::fabs(submission_result(row, col) - reference_result.at(row, col))
      };

      max_difference = std::max(max_difference, difference);
    }
  }

  return max_difference;
}

int run_public_cases() {
  int failures{};

  for (const CorrectnessCase& test_case : kPublicCases) {
    const double max_difference{max_difference_from_reference(
      test_case.rows, test_case.cols, test_case.steps, test_case.condition
    )};
    const bool passed{max_difference <= kTolerance};

    std::fprintf(
      stderr, "[%s] %s  (max_diff=%.3e)\n",
      passed ? "PASS" : "FAIL", test_case.name, max_difference
    );

    if (!passed) {
      ++failures;
    }
  }

  return failures;
}

int run_benchmark() {
  constexpr std::size_t kGridSize{1024};
  constexpr std::size_t kTimeSteps{200};

  Grid submission_first{kGridSize, kGridSize};
  Grid submission_second{kGridSize, kGridSize};
  ReferenceGrid reference_first{kGridSize, kGridSize};
  ReferenceGrid reference_second{kGridSize, kGridSize};

  for (std::size_t row{}; row < kGridSize; ++row) {
    for (std::size_t col{}; col < kGridSize; ++col) {
      const double temperature{
        initial_temperature(InitialCondition::kCenterBlock, kGridSize, kGridSize, row, col)
      };

      submission_first(row, col)   = temperature;
      reference_first.at(row, col) = temperature;
    }
  }

  Grid* submission_old{&submission_first};
  Grid* submission_new{&submission_second};

  const auto submission_start{Clock::now()};
  for (std::size_t step{}; step < kTimeSteps; ++step) {
    apply_stencil(*submission_old, *submission_new);
    std::swap(submission_old, submission_new);
  }
  const double submission_milliseconds{
    std::chrono::duration<double, std::milli>(Clock::now() - submission_start).count()
  };

  ReferenceGrid* reference_old{&reference_first};
  ReferenceGrid* reference_new{&reference_second};

  const auto reference_start{Clock::now()};
  for (std::size_t step{}; step < kTimeSteps; ++step) {
    apply_reference_stencil(*reference_old, *reference_new);
    std::swap(reference_old, reference_new);
  }
  const double reference_milliseconds{
    std::chrono::duration<double, std::milli>(Clock::now() - reference_start).count()
  };

  const double memory_megabytes{
    2.0 * static_cast<double>(kGridSize) * kGridSize * sizeof(double) / 1e6
  };
  const double score{
    submission_milliseconds > 0.0 ? reference_milliseconds / submission_milliseconds : 0.0
  };

  std::printf(
    "{\"runtime_ms\": %.3f, \"memory_mb\": %.3f, \"score\": %.3f}\n",
    submission_milliseconds, memory_megabytes, score
  );

  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--check") == 0) {
    return run_public_cases() == 0 ? 0 : 1;
  }

  return run_benchmark();
}
