# UWHPC Onboarding — 2D Heat Diffusion

Starter repository for the UWHPC onboarding problem.

Begin by completing the Google Form on the onboarding site below:

**The problem statement lives here: [UWHPC Onboarding](https://docs.uwhpc.com/onboarding/problem-statement/).** Read it first.
This file only covers building and submitting.

## What you edit

One file: [`src/submission.hpp`](src/submission.hpp). Both the `Grid` class and
the `apply_stencil` function go there, and the interface your `Grid` has to
provide is documented at the top of that file.

Everything else (`bench/`, `CMakeLists.txt`, `CMakePresets.json`, etc) is the
evaluation harness. **Don't edit it.**

## Build & run

Requires CMake ≥ 3.21, Ninja, and a C++17 compiler.

```bash
cmake --preset benchmark                        # configure
cmake --build --preset benchmark                # build
ctest --preset benchmark --output-on-failure    # correctness test
./build/benchmark/uwhpc_benchmark               # run the benchmark
```

`--check` runs four public correctness cases. The evaluator runs those plus
private cases you can't see here, so passing locally is necessary but not
sufficient.

The benchmark prints one line of JSON, where `score` is the harness's reference
time divided by yours:

```json
{ "runtime_ms": 148.404, "memory_mb": 16.777, "score": 1.293 }
```

Your `score` is measured against a reference **baseline**: a score of `1.0` ties
the baseline, above `1.0` beats it, and below means you're slower. **You need a
score over `0.9` to pass** — the evaluator marks any submission below that as a
failing check.

Use the commands above rather than an IDE's own CMake integration. Those default
to a Debug build, which will cost you most of your score. See the
[problem statement](https://docs.uwhpc.com/onboarding/problem-statement/) for what the number means.

## Submitting

Submissions are a **pull request from your own fork** — no credentials or
workflow changes needed.

**Fork** this repository, implement your solution in
`src/submission.hpp`, and open a **pull request** against
`UWHPC/onboarding-template`. The evaluator runs automatically and posts a result
comment on your PR with your build, test, and benchmark results. Keep pushing
commits and re-running until you're happy with your result — the comment updates
in place.

Once you're satisfied with your submission, join the UWHPC Discord from our
**[contact page](https://www.uwhpc.com/contact)** and make a post in the
**onboarding** forum channel with your name and a link to your pull request.
