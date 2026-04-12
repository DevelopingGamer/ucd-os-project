#ifndef FS_BENCH_H
#define FS_BENCH_H

#include "benchmark_common.h"

/*
 * fs_bench.h
 *
 * KPIs:
 *   - Filesystem sequential write throughput
 *   - Filesystem sequential read throughput
 *   - fsync / flush latency
 */

typedef struct {
    uint32_t fs_write_kbps;
    uint32_t fs_read_kbps;
    uint32_t fs_sync_us;
} fs_stats_t;

fs_stats_t fs_bench_run(void);

#endif /* FS_BENCH_H */
