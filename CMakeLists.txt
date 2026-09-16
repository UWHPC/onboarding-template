cmake_minimum_required(VERSION 3.21)
project(uwhpc_onboarding CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(uwhpc_benchmark bench/main.cpp
        src/naive.hpp
)
target_include_directories(uwhpc_benchmark PRIVATE src)

# OpenMP is optional for local builds (stock macOS Clang, for example, ships
# without it). The official evaluator builds with OpenMP enabled and
# -march=x86-64-v3 on x86, so `#pragma omp` code runs multithreaded there even
# if your local build is serial. Install libomp locally to test parallelism.
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(uwhpc_benchmark PRIVATE OpenMP::OpenMP_CXX)
endif()

enable_testing()
add_test(NAME correctness COMMAND uwhpc_benchmark --check)
