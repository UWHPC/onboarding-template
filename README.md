# UWHPC Onboarding — 2D Heat Diffusion

The submission is `src/submission.hpp`. The harness times a 1024×1024 grid for 200 steps. The center block starts at 100 and the rest of the field starts at 0. Score is `reference_ms / runtime_ms`. A score above 0.9 passes. Problem statement: [UWHPC Onboarding](https://docs.uwhpc.com/onboarding/problem-statement/).

## Techniques

- **Hot rectangle.** Nonzero values written at setup record a rectangle. Each step that rectangle grows by one cell on each side, and only cells inside it are updated. It starts at rows 384–639 and columns 384–639 (256×256). After 200 steps it is 656×656 (rows 184–839, columns 184–839) and does not reach the outer edge. Interior cells updated: 44,436,400. Full interior over the same 200 steps: 208,896,800. Ratio: 0.213.

- **Flat layout.** One allocation, aligned to 64 bytes. Each row is padded to a multiple of 8 doubles. A 1024-wide row is 8192 bytes, a multiple of 4096, so 8 more doubles are added. Stride on this grid: 1032 doubles. One grid: 8,454,144 bytes.

- **Vector interior.** Where the compiler provides AVX2 and FMA, interior columns are computed 4 doubles at a time. On arm64 they are computed 2 doubles at a time with NEON. Columns left over at the ends of a row are computed one at a time. The runs below used NEON.

- **OpenMP rows.** When the rectangle has at least 16 rows and 64 columns, rows are split with `schedule(static)`. The threads are started in the `Grid` constructor, before the harness starts the timer. On this machine the default team size is 10. Every timed step is above that size cutoff.

## Local harness times

Apple clang 17, arm64, `-O3`, five runs. Both builds use the hot rectangle, the padded layout, and NEON. They differ by whether OpenMP is linked.

| OpenMP | Runtime | Score |
| --- | --- | --- |
| off | 27.295–28.382 ms | 3.260–3.448 |
| on (10 threads) | 13.133–15.007 ms | 6.372–7.295 |

The score moves with the reference time on that run. These numbers are from my dev machine, not the evaluator.
