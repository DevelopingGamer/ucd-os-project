/*
 * mem_profile.c
 *
 * KPIs covered:
 *   - Memory footprint: compile-time (.text/.rodata, .data/.bss)
 *     Reported via linker symbols; see bench_log.c for the printf.
 *   - Memory footprint: runtime (heap used/free, low-water mark)
 *   - Task stack high-water mark (deepest stack usage observed)
 */

#include "benchmark_common.h"
#include "mem_profile.h"

/* ════════════════════════════════════════════════════════════════════
 *  FreeRTOS
 * ═══════════════════════════════════════════════════════════════════*/
#if defined(FREERTOS)

mem_results_t mem_profile_collect(void) {
    mem_results_t r = {0};
    HeapStats_t hs;
    vPortGetHeapStats(&hs);

    r.heap_free_bytes      = hs.xAvailableHeapSpaceInBytes;
    r.heap_total_bytes     = configTOTAL_HEAP_SIZE;
    r.heap_used_bytes      = r.heap_total_bytes - r.heap_free_bytes;
    r.heap_min_ever_free   = hs.xMinimumEverFreeBytesRemaining;
    r.task_stack_hwm_bytes = uxTaskGetStackHighWaterMark(NULL)
                             * sizeof(StackType_t);
    r.footprint_kb         = (r.heap_used_bytes + 512U) / 1024U;
    return r;
}

/* ════════════════════════════════════════════════════════════════════
 *  RIOT OS
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(RIOT_OS)
#include "thread.h"

extern char _sheap, _eheap;

mem_results_t mem_profile_collect(void) {
    mem_results_t r = {0};
    r.heap_total_bytes     = (uint32_t)(&_eheap - &_sheap);
    /* RIOT uses tlsf; approximate used from active thread stacks */
    uint32_t used = 0;
    for (int i = 0; i < MAXTHREADS; i++) {
        thread_t *t = (thread_t *)sched_threads[i];
        if (t) used += CONFIG_THREAD_STACKSIZE_DEFAULT;
    }
    r.heap_used_bytes      = used;
    r.heap_free_bytes      = (r.heap_total_bytes > used)
                             ? (r.heap_total_bytes - used) : 0;
    r.task_stack_hwm_bytes = thread_measure_stack_free(thread_get_active());
    r.footprint_kb         = (r.heap_used_bytes + 512U) / 1024U;
    return r;
}

/* ════════════════════════════════════════════════════════════════════
 *  Zephyr
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(ZEPHYR)
#include <zephyr/sys/sys_heap.h>

extern struct sys_heap _system_heap;

mem_results_t mem_profile_collect(void) {
    mem_results_t r = {0};
    struct sys_memory_stats hs;
    sys_heap_runtime_stats_get(&_system_heap, &hs);

    r.heap_used_bytes      = (uint32_t)hs.allocated_bytes;
    r.heap_free_bytes      = (uint32_t)hs.free_bytes;
    r.heap_total_bytes     = (uint32_t)(hs.allocated_bytes + hs.free_bytes);
    r.heap_min_ever_free   = r.heap_total_bytes
                             - (uint32_t)hs.max_allocated_bytes;
    r.task_stack_hwm_bytes = (uint32_t)k_thread_stack_space_get(
                                 k_current_get());
    r.footprint_kb         = (r.heap_used_bytes + 512U) / 1024U;
    return r;
}
#endif
