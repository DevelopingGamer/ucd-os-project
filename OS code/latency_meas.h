#ifndef LATENCY_MEAS_H
#define LATENCY_MEAS_H

#include "benchmark_common.h"

/*
 * latency_meas.h
 *
 * KPIs:
 *   - Interrupt latency (IRQ fire → ISR entry)
 *   - Response time    (ISR exit → highest-priority task runs)
 *   - Latency under load (same measurement during stress)
 */

typedef struct {
    uint32_t sum_us;
    uint32_t min_us;
    uint32_t max_us;
    uint32_t avg_us;
    uint32_t jitter_us;
} latency_stats_t;

latency_stats_t    latency_measure_irq(void);
latency_results_t  latency_measure_all(void);

#endif /* LATENCY_MEAS_H */
