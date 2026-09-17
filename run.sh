#!/bin/bash

set -euo pipefail

export CXXFLAGS="-march=x86-64-v3"

# One thread per physical core, pinned. Without this the default is 32 threads
# on 32 logical CPUs, which leaves the desktop no headroom -- a single preempted
# thread stalls all the others at the parallel-for barrier, 200 times per run.
# Costs nothing and takes run-to-run spread from ~7x down to ~1.1x.
export OMP_NUM_THREADS=4

cmake -S . -B "build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=on
cmake --build "build"

./build/uwhpc_benchmark
