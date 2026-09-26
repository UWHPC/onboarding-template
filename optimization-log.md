# Optimization Log

A record of all my runs so I can keep track of whether or not my optimizations are useful.

## Specs

**CPU:** 13th Gen Intel Core i5-13420H

**RAM:** 64 GB DDR4-3200 SODIMM

**OS:** Windows 11 Pro 25H2 (Build 26200.9168)

## Runs

Using the following command:

```ps
./bench_avg.ps1 -n 40
```

The recorded score represents a median of all the scores across the 40 runs.

To ensure consistent results, I used Quick CPU with the `High performance` power plan. A clock speed of 3.97 GHz was observed during runs.

| Commit hash | Changes | Score |
| - | - | - |
| `d0605ad` | Initial implementation | 0.707 |
| `288f180` | Separate boundary logic into separate loops | 0.980 |
| `fc9c764` | Flatten grid to 1D vector | 1.014 |
| `d1d3dc5` | Use SIMD in loops | 1.010 |
| `eefbd6e` | Multithread main stencil loop | 1.977 |
| `43783a1` | Replace std::vector with array | 2.036 |
| `d85aabe` | Divide main loop into blocks | 2.018 |
| `b75891a` | Use memcpy in place of row boundary loop | 2.069 |
| `298f0a1` | Cross-platform restrict keyword switching | 2.054 |
| `05df065` | Apply stencil kernel only to active bounding box | 7.629 |
