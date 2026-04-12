#ifndef BENCH_LOG_H
#define BENCH_LOG_H

#include "benchmark_common.h"

/*
 * bench_log.h
 *
 * Data collection methods:
 *   - Serial logging for runtime metrics
 *   - Kernel trace output for scheduling analysis
 *   - CSV output for statistical post-processing (repeated runs)
 *   - External monitoring: CSV importable by Python / Excel / logic analyser
 */

void bench_log_results(const bench_results_t *r);
void bench_log_csv_header(void);
void bench_log_csv_row(const bench_results_t *r);
void bench_trace_event(const char *event, const char *task, uint32_t pri);

#endif /* BENCH_LOG_H */
