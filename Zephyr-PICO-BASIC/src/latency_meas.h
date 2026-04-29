//Include guard
#ifndef LATENCY_MEAS_H
#define LATENCY_MEAS_H
//Header file import
#include "benchmark_common.h"

//Struct that includes the sum of latency,
// the minimum latency, the maximum latency,
// the average latency, and the difference
// between the maximum and minimum latency
typedef struct {
    uint32_t sum_us;
    uint32_t min_us;
    uint32_t max_us;
    uint32_t avg_us;
    uint32_t jitter_us;
} latency_stats_t;

//Function prototypes
latency_stats_t    latency_measure_irq(void);
latency_results_t  latency_measure_all(void);

//Include guard
#endif
