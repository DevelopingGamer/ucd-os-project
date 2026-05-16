#include "benchmark_common.h"
#include "mem_profile.h"

#if defined(FREERTOS_PICO)
extern char __bss_start__[];
extern char __bss_end__[];
extern char __data_start__[];
extern char __data_end__[];

mem_results_t mem_profile_collect(void) {
    //Initialize the metrics struct to all 0's
    mem_results_t r = {0};
    //Built-in FreeRTOS structure for memory data 
    HeapStats_t hs;
    //Fill the hs variable with the current state of the RAM
    vPortGetHeapStats(&hs);

    //Get the current amount of unused RAM
    r.heap_free_bytes      = hs.xAvailableHeapSpaceInBytes;
    //Get the total amount of RAM
    r.heap_total_bytes     = configTOTAL_HEAP_SIZE;
    //Calcuate the current amount of used RAM
    r.heap_used_bytes      = r.heap_total_bytes - r.heap_free_bytes;
    //Get the minimum amount of free RAM there has ever been since the
    // microcontroller has been booted
    r.heap_min_ever_free   = hs.xMinimumEverFreeBytesRemaining;
    //Get the number of bytes in the current task's stack that has
    // never been used
    r.task_stack_hwm_bytes = uxTaskGetStackHighWaterMark(NULL)
                             * sizeof(StackType_t);

    //Caluclate Static Memory Usage
    uint32_t static_bss  = (uint32_t)__bss_end__  - (uint32_t)__bss_start__;
    uint32_t static_data = (uint32_t)__data_end__ - (uint32_t)__data_start__;
    uint32_t total_static_ram = static_bss + static_data;
    //Get total usage
    uint32_t active_ram_bytes;
    //Find if the heap is a part of the BSS allocation and run accordingly
    if (total_static_ram >= r.heap_total_bytes) {
        active_ram_bytes = (total_static_ram - r.heap_total_bytes) + r.heap_used_bytes;
    } else {
        active_ram_bytes = total_static_ram + r.heap_used_bytes;
    }
    //Convert to kilobytes
    r.footprint_kb         = (r.active_ram_bytes + 512U) / 1024U;
    //Return the struct
    return r;
}

#elif defined(RIOT_OS)
#include "thread.h"
//Get start of heap and end of heap from the RAM chip
extern char _sheap, _eheap;

mem_results_t mem_profile_collect(void) {
    //Initialize struct values to 0
    mem_results_t r = {0};
    //Get the total number of bytes RIOT has allocated for dynamic use
    r.heap_total_bytes     = (uint32_t)(&_eheap - &_sheap);
    //Initalize the amount of memory currently being used to 0
    uint32_t used = 0;
    //Iterate through each thread currently running on the microcontroller
    // and if it's currently active add its stack size to the used counter
    for (int i = 0; i < MAXTHREADS; i++) {
        thread_t *t = (thread_t *)sched_threads[i];
        if (t) used += THREAD_STACKSIZE_DEFAULT;
    }
    //Record the amount of stack memory used by each thread and get the
    // difference between the total heap allocate and stack memory usage
    r.heap_used_bytes      = used;
    r.heap_free_bytes      = (r.heap_total_bytes > used)
                             ? (r.heap_total_bytes - used) : 0;
    //Get the number of bytes in the current thread's stack that are currently
    // free
    r.task_stack_hwm_bytes = thread_measure_stack_free(thread_get_active());
    //Get heap_used_bytes in kilobytes
    r.footprint_kb         = (r.heap_used_bytes + 512U) / 1024U;
    //Return the struct
    return r;
}

#elif defined(ZEPHYR)
#include <zephyr/sys/sys_heap.h>
//Get standard Zephyr linker symbols for RAM tracking
extern char __bss_start[];
extern char __bss_end[];
extern char __data_region_start[];
extern char __data_region_end[];

// Get the heap tracker managed by the Zephyr kernel
extern struct sys_heap _system_heap;

mem_results_t mem_profile_collect(void) {
    //Initialize all struct values to 0
    mem_results_t r = {0};
    //Initalize sys_memory_stats struct from zephyr/sys/mem_stats.h (pulled by
    // zephyr/sys/sys_heap.h) to store allocated_bytes, free_bytes, and
    // max_allocated bytes
    struct sys_memory_stats hs;
    //Get dynamic heap usage
    sys_heap_runtime_stats_get(&_system_heap, &hs);
    
    r.heap_used_bytes      = (uint32_t)hs.allocated_bytes;
    r.heap_free_bytes      = (uint32_t)hs.free_bytes;
    r.heap_total_bytes     = (uint32_t)(hs.allocated_bytes + hs.free_bytes);
    r.heap_min_ever_free   = r.heap_total_bytes - (uint32_t)hs.max_allocated_bytes;

    //Calculate static RAM usage
    uint32_t static_data = (uint32_t)__data_region_end - (uint32_t)__data_region_start;
    uint32_t static_bss  = (uint32_t)__bss_end  - (uint32_t)__bss_start;
    uint32_t total_static_ram = static_data + static_bss;

    //Get the footprint
    uint32_t active_ram_bytes = (total_static_ram - r.heap_total_bytes) + r.heap_used_bytes;

    //Convert to KB with proper rounding
    r.footprint_kb = (active_ram_bytes + 512U) / 1024U;
    return r;
}
#endif
