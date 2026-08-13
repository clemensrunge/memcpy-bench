#include <stdio.h>

#include "Benchmark.h"
#include "CpuIdFunctions.h"
#include "MemoryInfo.h"

int main(int argc, char** argv)
{
    memory_maybe_prompt_elevate(argc, argv);

    CpuInfo cpu_info = cpuid_get_info();
    cpuid_print_info(&cpu_info);

    MemoryInfo mem_info = memory_get_info();
    memory_print_info(&mem_info);

    putchar('\n');

    BenchmarkResults bench = benchmark_run();
    benchmark_print(&bench, &cpu_info);

    return 0;
}
