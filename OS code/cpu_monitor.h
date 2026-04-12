#ifndef CPU_MONITOR_H
#define CPU_MONITOR_H

#include "benchmark_common.h"

/*
 * cpu_monitor.h
 *
 * KPIs:
 *   - CPU utilisation under idle conditions
 *   - CPU utilisation under load conditions
 */

cpu_results_t cpu_monitor_sample(void);

#endif /* CPU_MONITOR_H */
