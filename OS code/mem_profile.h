#ifndef MEM_PROFILE_H
#define MEM_PROFILE_H

#include "benchmark_common.h"

/*
 * mem_profile.h
 *
 * KPIs:
 *   - Memory footprint: compile-time (.text, .data, .bss via linker symbols)
 *   - Memory footprint: runtime (heap used/free, stack high-water mark)
 */

typedef struct {
    uint32_t heap_used_bytes;
    uint32_t heap_free_bytes;
    uint32_t heap_total_bytes;
    uint32_t heap_min_ever_free;
    uint32_t task_stack_hwm_bytes;
    uint32_t footprint_kb;
} mem_stats_t;

mem_results_t mem_profile_collect(void);

#endif /* MEM_PROFILE_H */
