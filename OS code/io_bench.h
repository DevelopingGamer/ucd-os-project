#ifndef IO_BENCH_H
#define IO_BENCH_H

#include "benchmark_common.h"

/*
 * io_bench.h
 *
 * KPIs:
 *   - I/O throughput (UART/serial)
 *   - Packet loss rate
 */

typedef struct {
    uint32_t throughput_kbps;
    float    packet_loss_pct;
} io_stats_t;

io_stats_t io_bench_run(void);

#endif /* IO_BENCH_H */
