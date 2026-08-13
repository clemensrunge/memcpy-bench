#include "Benchmark.h"
#include "Util.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>

// Each buffer size copies roughly this many total bytes, so small buffers
// (many fast iterations) and large buffers (few slow iterations) both get
// a stable, comparable timing measurement instead of a fixed iteration count.
#define TOTAL_WORK_BYTES (512UL * 1024 * 1024)
#define MIN_ITERS 4UL

static SpeedResult memcpy_speed(unsigned long buf_size)
{
    unsigned long iters = TOTAL_WORK_BYTES / buf_size;
    if (iters < MIN_ITERS)
    {
        iters = MIN_ITERS;
    }

    struct timeval start, end;
    unsigned char* pbuff_1 = malloc(buf_size);
    unsigned char* pbuff_2 = malloc(buf_size);

    gettimeofday(&start, NULL);
    for (unsigned long i = 0; i < iters; ++i)
    {
        memcpy(pbuff_2, pbuff_1, buf_size);
        // Compiler memory barrier: without this, an optimizing build can prove
        // pbuff_2's contents are never read and eliminate (or coalesce) the
        // copies entirely, collapsing the loop to ~0 time and yielding "inf".
        __asm__ volatile("" : : "r"(pbuff_2) : "memory");
    }
    gettimeofday(&end, NULL);

    double elapsed_sec = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_usec - start.tv_usec) / 1e6;
    double mb_per_sec = ((double)buf_size * (double)iters / (1024.0 * 1024.0)) / elapsed_sec;

    free(pbuff_1);
    free(pbuff_2);

    return (SpeedResult){ .buf_size = buf_size, .iters = iters, .mb_per_sec = mb_per_sec };
}

BenchmarkResults benchmark_run(void)
{
    BenchmarkResults bench;
    bench.count = 0;

    // Double the buffer size each step (1 KB -> 64 MB) so the run lands on
    // typical L1 (~32 KB), L2 (~256 KB-1 MB), and L3 (~8-32 MB) cache
    // boundaries, with a few points beyond L3 to show main-memory bandwidth.
    for (unsigned long buf_size = 1024;
         buf_size <= 64UL * 1024 * 1024 && bench.count < BENCHMARK_MAX_RESULTS; buf_size *= 2)
    {
        bench.results[bench.count++] = memcpy_speed(buf_size);
    }

    bench.fastest = 0;
    for (int i = 1; i < bench.count; ++i)
    {
        if (bench.results[i].mb_per_sec > bench.results[bench.fastest].mb_per_sec)
        {
            bench.fastest = i;
        }
    }

    return bench;
}

// Labels buffer sizes as e.g. "512K" / "64M" -- these are always <= 4 chars
// for the power-of-two sizes this benchmark uses, so they fit BAR_COL_WIDTH.
static void format_size_label(unsigned long buf_size, char* out, size_t out_len)
{
    const char* unit;
    unsigned int value = size_kb_for_display((unsigned int)(buf_size / 1024), &unit);
    snprintf(out, out_len, "%u%c", value, unit[0]);
}

// The machine's largest detected cache per level, derived once per report.
typedef struct {
    unsigned long l1_kb;
    unsigned long l2_kb;
    unsigned long l3_kb;
} CacheTiers;

static CacheTiers cache_tiers(const CpuInfo* cpu_info)
{
    return (CacheTiers){
        .l1_kb = cpuid_cache_size_kb(cpu_info, 1),
        .l2_kb = cpuid_cache_size_kb(cpu_info, 2),
        .l3_kb = cpuid_cache_size_kb(cpu_info, 3),
    };
}

// Classifies a buffer size against the machine's actual detected cache sizes.
// Both the source and destination buffers must fit for a copy to stay within
// a given tier, so the classification is based on 2x buf_size.
static const char* classify_cache_level(const CacheTiers* tiers, unsigned long buf_size)
{
    unsigned long working_set_kb = (buf_size * 2) / 1024;

    if (tiers->l1_kb > 0 && working_set_kb <= tiers->l1_kb)
    {
        return "L1";
    }
    if (tiers->l2_kb > 0 && working_set_kb <= tiers->l2_kb)
    {
        return "L2";
    }
    if (tiers->l3_kb > 0 && working_set_kb <= tiers->l3_kb)
    {
        return "L3";
    }
    return "RAM";
}

// One rate unit (KB/s, MB/s, GB/s, TB/s) is picked for the whole report,
// based on the fastest result, so every table row and chart label reads in
// the same unit instead of switching between rows.
typedef struct {
    const char* unit; // e.g. "GB/s"
    double divisor;   // how many MB/s one unit represents
} RateUnit;

static RateUnit pick_rate_unit(double mb_per_sec)
{
    if (mb_per_sec >= 1024.0 * 1024.0)
    {
        return (RateUnit){ "TB/s", 1024.0 * 1024.0 };
    }
    if (mb_per_sec >= 1024.0)
    {
        return (RateUnit){ "GB/s", 1024.0 };
    }
    if (mb_per_sec >= 1.0)
    {
        return (RateUnit){ "MB/s", 1.0 };
    }
    return (RateUnit){ "KB/s", 1.0 / 1024.0 };
}

// Formats a value already scaled into the report's rate unit.
static void format_scaled_rate(double scaled, const char* unit, char* out, size_t out_len)
{
    if (fabs(scaled - round(scaled)) < 0.05)
    {
        snprintf(out, out_len, "%.0f %s", round(scaled), unit);
    }
    else
    {
        snprintf(out, out_len, "%.1f %s", scaled, unit);
    }
}

static void format_rate_label(double mb_per_sec, RateUnit rate, char* out, size_t out_len)
{
    format_scaled_rate(mb_per_sec / rate.divisor, rate.unit, out, out_len);
}

static void print_result(const SpeedResult* r, bool is_fastest, const CacheTiers* tiers, RateUnit rate)
{
    char size_str[24];
    format_size_label(r->buf_size, size_str, sizeof(size_str));

    char rate_str[16];
    format_rate_label(r->mb_per_sec, rate, rate_str, sizeof(rate_str));

    printf("%10lu  %8s  %5s  %6lu  %11s  %s\n",
           r->buf_size, size_str, classify_cache_level(tiers, r->buf_size), r->iters, rate_str,
           is_fastest ? "<-- fastest" : "");
}

#define CHART_HEIGHT 15
#define BAR_COL_WIDTH 7
#define ROW_LABEL_WIDTH 11
#define TARGET_TICKS 5

static void print_centered(const char* s, int width)
{
    int pad = width - (int)strlen(s);
    if (pad < 0)
    {
        pad = 0;
    }
    int left = pad / 2;
    int right = pad - left;
    printf("%*s%s%*s", left, "", s, right, "");
}

// Smallest power of two >= x.
static double round_up_pow2(double x)
{
    double p = 1.0;
    while (p < x)
    {
        p *= 2.0;
    }
    return p;
}

static void print_chart(const SpeedResult* results, int count, int fastest, const CacheTiers* tiers,
                         RateUnit rate)
{
    // All axis math happens in the report's rate unit ("scaled" values); only
    // the per-bar fill threshold converts back to MB/s to compare against
    // the raw results.
    double max_scaled = results[fastest].mb_per_sec / rate.divisor;

    // Pick a step that is itself a power of two (e.g. 32, 64 GB/s) -- the
    // kind of round number commonly used as a bandwidth reference point --
    // then make the chart top the smallest multiple of that step covering
    // the tallest bar. Gridlines land on step, 2*step, 3*step, ...
    double step_scaled = round_up_pow2(max_scaled / TARGET_TICKS);
    double chart_top_scaled = ceil(max_scaled / step_scaled) * step_scaled;

    char top_label[16];
    format_scaled_rate(chart_top_scaled, rate.unit, top_label, sizeof(top_label));
    printf("\nThroughput -- bar height proportional to value, top = %s\n", top_label);

    double tick_scaled[CHART_HEIGHT + 1];
    bool has_tick[CHART_HEIGHT + 1];
    for (int i = 0; i <= CHART_HEIGHT; ++i)
    {
        has_tick[i] = false;
    }

    int num_ticks = (int)round(chart_top_scaled / step_scaled);
    for (int k = 1; k <= num_ticks; ++k)
    {
        double val_scaled = k * step_scaled;
        int row = (int)round(val_scaled / chart_top_scaled * CHART_HEIGHT);
        if (row >= 0 && row <= CHART_HEIGHT)
        {
            has_tick[row] = true;
            tick_scaled[row] = val_scaled;
        }
    }

    for (int row = CHART_HEIGHT; row >= 1; --row)
    {
        double threshold = chart_top_scaled * rate.divisor * row / CHART_HEIGHT;

        if (has_tick[row])
        {
            char row_label[16];
            format_scaled_rate(tick_scaled[row], rate.unit, row_label, sizeof(row_label));
            printf("%*s |", ROW_LABEL_WIDTH, row_label);
        }
        else
        {
            printf("%*s |", ROW_LABEL_WIDTH, "");
        }

        for (int c = 0; c < count; ++c)
        {
            // epsilon guards against float rounding excluding the tallest bar
            bool filled = results[c].mb_per_sec + 1e-6 >= threshold;
            for (int w = 0; w < BAR_COL_WIDTH; ++w)
            {
                // Leave the first/last column blank so adjacent bars don't
                // visually run together.
                bool edge = (w == 0 || w == BAR_COL_WIDTH - 1);
                putchar((filled && !edge) ? '#' : ' ');
            }
            putchar(' ');
        }
        putchar('\n');
    }

    printf("%*s +", ROW_LABEL_WIDTH, "");
    for (int c = 0; c < count; ++c)
    {
        for (int w = 0; w < BAR_COL_WIDTH; ++w)
        {
            putchar('-');
        }
        putchar('-');
    }
    putchar('\n');

    printf("%*s  ", ROW_LABEL_WIDTH, "");
    for (int c = 0; c < count; ++c)
    {
        char label[24];
        format_size_label(results[c].buf_size, label, sizeof(label));
        print_centered(label, BAR_COL_WIDTH);
        putchar(' ');
    }
    putchar('\n');

    printf("%*s  ", ROW_LABEL_WIDTH, "");
    for (int c = 0; c < count; ++c)
    {
        print_centered(classify_cache_level(tiers, results[c].buf_size), BAR_COL_WIDTH);
        putchar(' ');
    }
    putchar('\n');
}

void benchmark_print(const BenchmarkResults* bench, const CpuInfo* cpu_info)
{
    RateUnit rate = pick_rate_unit(bench->results[bench->fastest].mb_per_sec);
    CacheTiers tiers = cache_tiers(cpu_info);

    printf("%10s  %8s  %5s  %6s  %11s\n", "bytes", "size", "cache", "iters", rate.unit);
    for (int i = 0; i < bench->count; ++i)
    {
        print_result(&bench->results[i], i == bench->fastest, &tiers, rate);
    }

    print_chart(bench->results, bench->count, bench->fastest, &tiers, rate);
}
