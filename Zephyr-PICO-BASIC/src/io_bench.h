//Include guard
#ifndef IO_BENCH_H
#define IO_BENCH_H
//Included library
#include "benchmark_common.h"
//Struct that stores throughput
// in kbps and the packet loss
// percentage
typedef struct {
    uint32_t throughput_kbps;
    float    packet_loss_pct;
} io_stats_t;
//Function prototype
io_stats_t io_bench_run(void);
//Include guard
#endif
