#ifndef TASK_SCHED_H
#define TASK_SCHED_H

#include "benchmark_common.h"

/*
 * task_sched.h
 *
 * KPIs:
 *   - Context switch overhead (round-trip semaphore yield timing)
 *   - Maximum concurrent tasks before failure
 */

uint32_t task_sched_measure_ctx_switch(void);
uint32_t task_sched_max_concurrent(void);

#endif /* TASK_SCHED_H */
