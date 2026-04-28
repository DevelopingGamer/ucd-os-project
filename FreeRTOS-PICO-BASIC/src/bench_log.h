//Include guard
#ifndef BENCH_LOG_H
#define BENCH_LOG_H
//Included library
#include "benchmark_common.h"
//Function prototypes
void bench_log_results(const bench_results_t *r);
void bench_log_csv_header(void);
void bench_log_csv_row(const bench_results_t *r);
void bench_trace_event(const char *event, const char *task, uint32_t pri);
//Include guard
#endif