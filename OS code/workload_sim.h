#ifndef WORKLOAD_SIM_H
#define WORKLOAD_SIM_H

#include "benchmark_common.h"

/*
 * workload_sim.h
 *
 * Workload scenarios:
 *   - Sensor data generation and periodic sampling  (100 Hz, 1000 samples)
 *   - Network message transmission                  (200 msgs, 64 B each)
 *   - Concurrent task execution with varying priorities (8 tasks, 4 levels)
 *   - Stress testing with high-frequency interrupts (5 kHz, 2 seconds)
 *
 * Also captures:
 *   - Deadline miss rate
 *   - Priority inversion detection
 */

#define SENSOR_PERIOD_MS      10U
#define SENSOR_TOTAL_SAMPLES  1000U
#define NET_MSG_COUNT         200U
#define NET_MSG_SIZE          64U
#define CONCURRENT_TASKS      8U
#define HF_IRQ_RATE_HZ        5000U
#define HF_IRQ_DURATION_MS    2000U
#define STRESS_REPEAT_RUNS    5U
#define DEADLINE_SLACK_US     500U

workload_results_t workload_run(void);

#endif /* WORKLOAD_SIM_H */
