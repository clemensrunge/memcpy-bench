#ifndef BENCHMARK_H
#define BENCHMARK_H

#include "CpuIdFunctions.h"

// The sweep doubles from 1 KB to 64 MB = 17 steps; a little headroom on top.
#define BENCHMARK_MAX_RESULTS 24

typedef struct {
    unsigned long buf_size;
    unsigned long iters;
    double mb_per_sec;
} SpeedResult;

typedef struct {
    SpeedResult results[BENCHMARK_MAX_RESULTS];
    int count;
    int fastest; // index into results[] of the highest mb_per_sec
} BenchmarkResults;

// Runs the memcpy bandwidth sweep (1 KB -> 64 MB, doubling) and returns the
// raw measurements. Does no printing.
BenchmarkResults benchmark_run(void);

// Prints the results table and an ASCII bar chart, using cpu_info to
// annotate which cache tier (L1/L2/L3/RAM) each buffer size falls into.
void benchmark_print(const BenchmarkResults* bench, const CpuInfo* cpu_info);

#endif // BENCHMARK_H
