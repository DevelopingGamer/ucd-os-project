//Include guard
#ifndef FS_BENCH_H
#define FS_BENCH_H
//Included header
#include "benchmark_common.h"
//Struct to store write speed, read speed, and
// the time it takes to write data from ram to
// storage hardware
typedef struct {
    uint32_t fs_write_kbps;
    uint32_t fs_read_kbps;
    uint32_t fs_sync_us;
} fs_stats_t;
//function prototype
fs_stats_t fs_bench_run(void);
//Include guard
#endif 