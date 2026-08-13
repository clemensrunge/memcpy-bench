# memcpy-bench

A small CLI tool that benchmarks `memcpy` bandwidth across buffer sizes from
1 KB to 64 MB and reports it alongside the machine's actual CPU, cache, and
RAM specs — so the drop-offs in the chart can be read in context ("this cliff
is the L2 boundary", "this floor is main-memory bandwidth").

Each buffer size is classified into the cache tier (L1/L2/L3/RAM) its working
set fits into, based on the real cache sizes detected via CPUID — not
hardcoded assumptions.

## Build

```bash
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release
```

## Run

```bash
./cmake-build-release/memcpy-bench
```

RAM module info (manufacturer/type/speed and the theoretical peak bandwidth
derived from it) comes from SMBIOS via `dmidecode`, which requires root. On
an interactive terminal the program offers to re-run itself with `sudo`;
without root it simply skips that section and everything else still works.

## Example output

```
CPU Model: AMD Ryzen 7 5700X 8-Core Processor
CPU Vendor: AuthenticAMD
Using Legacy AMD method
L1 Data Cache:             32 KB
L1 Instruction Cache:      32 KB
L2 Unified Cache:         512 KB
L3 Unified Cache:          32 MB
Memory: 4x 8 GB DDR4 @ 3200 MT/s -- theoretical peak: 51.2 GB/s (2 channels x 25.6 GB/s)

     bytes      size  cache   iters         GB/s
      1024        1K     L1  524288   101.4 GB/s
      2048        2K     L1  262144   110.2 GB/s
      4096        4K     L1  131072   125.1 GB/s
      8192        8K     L1   65536   133.4 GB/s  <-- fastest
     16384       16K     L1   32768   128.8 GB/s
     32768       32K     L2   16384    60.9 GB/s
     65536       64K     L2    8192    65.9 GB/s
    131072      128K     L2    4096    65.9 GB/s
    262144      256K     L2    2048    55.3 GB/s
    524288      512K     L3    1024    47.4 GB/s
   1048576        1M     L3     512    47.2 GB/s
   2097152        2M     L3     256    45.5 GB/s
   4194304        4M     L3     128    39.8 GB/s
   8388608        8M     L3      64    34.4 GB/s
  16777216       16M     L3      32    20.2 GB/s
  33554432       32M    RAM      16    11.9 GB/s
  67108864       64M    RAM       8     8.1 GB/s

Throughput -- bar height proportional to value, top = 160 GB/s
   160 GB/s |
            |
            |
   128 GB/s |                         #####   #####
            |                 #####   #####   #####
            |         #####   #####   #####   #####
    96 GB/s | #####   #####   #####   #####   #####
            | #####   #####   #####   #####   #####
            | #####   #####   #####   #####   #####
    64 GB/s | #####   #####   #####   #####   #####           #####   #####
            | #####   #####   #####   #####   #####   #####   #####   #####   #####
            | #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####
    32 GB/s | #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####
            | #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####
            | #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####   #####
            +----------------------------------------------------------------------------------------------------------------------------------------
               1K      2K      4K      8K      16K     32K     64K    128K    256K    512K     1M      2M      4M      8M      16M     32M     64M
               L1      L1      L1      L1      L1      L2      L2      L2      L2      L3      L3      L3      L3      L3      L3      RAM     RAM
```

Reading it: throughput is highest while both the source and destination
buffer fit in L1, then steps down each time the working set (2x the buffer
size) outgrows a cache tier, bottoming out near main-memory bandwidth for
the largest buffers. The 32M/64M rows sit close to what the RAM's rated
speed predicts.

## How it works

- **`CpuIdFunctions`** — vendor, model name, and L1/L2/L3 cache sizes read
  directly from CPUID leaves (deterministic cache enumeration, with a legacy
  AMD fallback for CPUs that don't support it).
- **`MemoryInfo`** — per-DIMM manufacturer/type/speed parsed from
  `dmidecode -t 17`; channel count inferred from distinct `Bank Locator`
  groupings; theoretical peak = MT/s × 8 bytes (64-bit bus) × channels.
- **`Benchmark`** — the sweep doubles the buffer size each step and scales
  the iteration count inversely, so every step copies roughly the same total
  bytes. Each `memcpy` is followed by a compiler memory barrier so optimized
  builds can't eliminate the "unused" copies (which would collapse the
  measured time to zero).

Measured throughput uses binary units (1 GB/s = 1024 MB/s), matching the
buffer-size labels; the RAM theoretical peak uses decimal units, matching how
memory speed is conventionally marketed. Chart gridlines land on power-of-two
values (32/64/96/128 GB/s) for easy reference.

## Platform

Linux, x86-64 (uses CPUID intrinsics, `gettimeofday`, `dmidecode`). Builds
with GCC or Clang via CMake; requires nothing beyond a C11/C++17 toolchain.
