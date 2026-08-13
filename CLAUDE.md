# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

A CMake-based CLI tool (mixed C11 / C++17) that benchmarks `memcpy` bandwidth across buffer sizes from 1 KB to 64 MB, and reports it alongside the machine's actual CPU/cache/RAM specs so the results can be read in context (e.g. "this drop-off happens right at the L2 boundary").

## Build

```bash
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug

# Release matters here -- see "Compiler memory barrier" below
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release
```

## Run

```bash
./cmake-build-debug/memcpy-bench
```

Without root, memory module info (manufacturer/type/speed) is skipped; the program offers an interactive re-exec via `sudo` on a TTY (see `MemoryInfo.c`).

## Architecture

- `main.c` — thin entry point. Calls three independent getter/printer pairs, in order: `memory_maybe_prompt_elevate`, then CPUID info, then memory info, then the benchmark. No logic of its own beyond sequencing.
- `CpuIdFunctions.h` / `.cpp` — vendor string, brand string, and L1/L2/L3 cache sizes via raw CPUID leaves (with an AMD-legacy fallback path for CPUs that don't support the deterministic cache leaf). This is C++ only because it needs `_MSC_VER`/`__cpuid_count` register-reference intrinsics; the header is `extern "C"` with plain-C-compatible structs so `main.c` and `Benchmark.c` can consume it directly.
- `MemoryInfo.h` / `.c` — plain C. RAM module info (manufacturer/type/speed/channel count) comes from `dmidecode -t 17`, which requires root — there is no unprivileged, portable way to read per-DIMM SPD data. `memory_get_info()` returns `available = false` (with no error, no crash) when not root; `memory_print_info()` silently prints nothing in that case. Channel count is inferred by counting distinct `Bank Locator` values across populated DIMMs (only trusted if it evenly divides the module count) — not from a hardcoded CPU/platform table.
- `Benchmark.h` / `.c` — plain C. `benchmark_run()` does the `memcpy` sweep (buffer size doubles each step; iteration count scales inversely so every step copies roughly the same total bytes) and returns raw results; `benchmark_print()` renders the results table and an ASCII bar chart. Cache-tier classification (L1/L2/L3/RAM) per buffer size uses `2 * buf_size` against `CpuInfo`'s real detected cache sizes, since both the source and destination buffers must fit for a copy to stay in that tier.
- `Util.h` — header-only (`static inline`) helpers shared across the C and C++ modules: `str_trim` and `size_kb_for_display` (the single owner of the "below 1 MB reads in KB, at/above in MB" display convention).
- `CMakeLists.txt` — `project(... C CXX)` because of the CPUID file; links `libm` explicitly on Unix (needed for `round`/`fabs`, not implicitly linked by GCC).

## Non-obvious behavior worth knowing before touching this code

- **Compiler memory barrier in the benchmark loop** (`Benchmark.c`): each `memcpy` is followed by `__asm__ volatile("" : : "r"(pbuff_2) : "memory")`. Without it, an optimizing build (`-O2`+) can prove the destination buffer is never read and either eliminate the copy loop entirely or collapse repeated iterations into one, making the measured time collapse to ~0 and the throughput calculation divide by zero → prints `inf`. This only reproduces in Release builds, not Debug — always sanity-check benchmark changes against a Release build, not just Debug.
- **Two different GB/s conventions, intentionally**: measured `memcpy` throughput uses binary units (÷1024) via `pick_rate_unit`/`format_rate_label` in `Benchmark.c`, matching how buffer sizes are labeled (`1K`, `1M`, ...). The RAM theoretical-peak bandwidth in `MemoryInfo.c` uses decimal units (÷1000), matching how RAM speed is conventionally marketed (e.g. "DDR4-3200 = 25.6 GB/s/channel"). This mismatch is deliberate, not a bug.
- **Chart y-axis ticks are powers of two** (e.g. 32/64/96/128 GB/s), not evenly divided fractions of the max — `round_up_pow2` in `Benchmark.c` picks a step that is itself a power of two so gridlines land on round, commonly-referenced bandwidth numbers.
