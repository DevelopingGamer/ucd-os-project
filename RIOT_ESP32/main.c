#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "ztimer.h"
#include "esp_timer.h"
#include "thread.h"
#include "sched.h"
#include "xtimer.h"

//Include Espressif system API for rebooting and RTC attributes
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"

extern char _sheap;
extern char _eheap;

/* Ensure scheduler statistics tracking features are available */
#ifdef MODULE_SCHEDSTATISTICS
#include "schedstatistics.h"
#endif

// Variables assigned to RTC fast memory do not reset during a software reboot
RTC_NOINIT_ATTR static uint32_t run_counter;
RTC_NOINIT_ATTR static uint32_t accumulated_us;

int boot_times(uint32_t runs) {
    int64_t boot_time_us = esp_timer_get_time();
    // uint32_t boot_time_ms = (uint32_t)(boot_time_us / 1000);

    // Determine if this is a fresh manual power-on or an automated cycle
    if (run_counter > runs || run_counter == 0) {
        run_counter = 1;
        accumulated_us = 0;
        printf("\n--- Starting New Boot Test Series ---\n");
    }

    // Accumulate current run metrics
    accumulated_us += boot_time_us;
    printf("Run [%lu/%lu]: Booted in %lu us\n", (unsigned long)run_counter, (unsigned long)runs, (unsigned long)boot_time_us);

    if (run_counter >= runs) {
        int64_t average_us = accumulated_us / runs;

        printf("\n====================================\n");
        printf("  TEST COMPLETE: Average Boot Time = %lu us\n", (unsigned long)average_us);
        printf("====================================\n\n");

        return average_us;
    } else {
        run_counter++;

        puts("Restarting the ESP32 now...");
        __asm__ __volatile__ ("call0 0x40000400");

        return 1;
    }
}

uint32_t sleep_recovery(void)
{
    int64_t recovery_time_us = esp_timer_get_time();

    // Determine what triggered the current system startup sequence
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    printf("\n=============================================\n");
    printf("         SYSTEM RECOVERY BENCHMARK           \n");
    printf("=============================================\n");

    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        // The device was in deep sleep and woke up via the RTC timer
        printf(" Wakeup Status: DEEP SLEEP RECOVERY SUCCESSFUL\n");
        printf(" Recovery Time: %lu us (~%lu ms)\n", (unsigned long)recovery_time_us, (unsigned long)(recovery_time_us / 1000));
        printf(" (Time taken from hardware wake to RIOT main execution context)\n");
        printf("=============================================\n\n");
    } else {
        // The device experienced a fresh power-on or standard hard reset
        printf(" Wakeup Status: COLD START / POWER-ON RESET\n");
        printf(" Preparing recovery testing profile...\n");
        printf(" Setting RTC wakeup timer for 4 seconds...\n");
        
        // Configure the ESP32 RTC controller to wake up in 4,000,000 microseconds
        esp_sleep_enable_timer_wakeup(4000000);

        printf(" Entering deep sleep state now. Serial terminal will disconnect...\n");
        printf("=============================================\n\n");
        
        // Flush remaining characters out of the physical UART buffer
        fflush(stdout);
        ztimer_sleep(ZTIMER_MSEC, 500);

        esp_deep_sleep_start();
    }

    return recovery_time_us;
}

int* cpu_load(void)
{
#ifndef MODULE_SCHEDSTATISTICS
    printf("Error: 'schedstatistics' module is missing from the Makefile.\n");
    return;
#else
    uint32_t total_runtime = 0;
    uint32_t idle_runtime = 0;
    int16_t idle_pid = KERNEL_PID_UNDEF;
    static int percent_arr[2];

    // 1. Identify the Process ID (PID) of the system idle thread
    for (int i = KERNEL_PID_FIRST; i <= KERNEL_PID_LAST; i++) {
        thread_t *t = (thread_t *)thread_get(i);
        if (t && t->name && (strcmp(t->name, "idle") == 0 || strcmp(t->name, "idle_thread") == 0)) {
            idle_pid = i;
            break;
        }
    }

    // 2. Sum up total system runtime and isolate idle thread time
    for (int i = KERNEL_PID_FIRST; i <= KERNEL_PID_LAST; i++) {
        if (sched_pidlist[i].laststart != 0 || sched_pidlist[i].runtime_us != 0) {
            // sched_pidlist[i].runtime_ticks tracks accumulated execution units
            uint32_t thread_time = sched_pidlist[i].runtime_us;
            total_runtime += thread_time;
            
            if (i == idle_pid) {
                idle_runtime = thread_time;
            }
        }
    }

    // Compute and display the final CPU load metrics
    if (total_runtime > 0) {
        uint32_t active_runtime = total_runtime - idle_runtime;
        
        /* Fixed-point arithmetic calculates percentage without floating-point bloat */
        uint32_t cpu_load_percent = (active_runtime * 100) / total_runtime;
        uint32_t cpu_load_fraction = ((active_runtime * 10000) / total_runtime) % 100;

        percent_arr[0] = cpu_load_percent;
        percent_arr[1] = cpu_load_fraction;

        printf("\n\n--- CPU Load Analysis ---\n");
        printf("Total Runtime Units : %lu\n", (unsigned long)total_runtime);
        printf("Idle Thread Units  : %lu\n", (unsigned long)idle_runtime);
        printf("Active Thread Units: %lu\n", (unsigned long)active_runtime);
        printf("Current CPU Load   : %lu.%02lu %%\n", (unsigned long)cpu_load_percent, 
                                                       (unsigned long)cpu_load_fraction);
        printf("-------------------------\n");

        return percent_arr;
    } else {
        printf("CPU Load tracking warming up... Try again on next interval.\n");
        return percent_arr;
    }
#endif
}

int memory_footprint(void)
{
    printf("\n=============================================\n");
    printf("         SYSTEM MEMORY FOOTPRINT             \n");
    printf("=============================================\n");

    /* 1. Extract Hardware Heap Space Metrics */
    struct mallinfo mi = mallinfo();

    uint32_t used_heap = (uint32_t)mi.uordblks;
    uint32_t free_heap = (uint32_t)mi.fordblks;
    uint32_t total_heap = used_heap + free_heap;

    printf("--- Heap Metrics ---\n");
    printf("  Total Managed Heap Space : %lu bytes\n", (unsigned long)total_heap);
    printf("  Allocated/Used Heap Space: %lu bytes\n", (unsigned long)used_heap);
    printf("  Remaining Free Heap Space: %lu bytes\n", (unsigned long)free_heap);

    /* 2. Map Active Thread Stack Space Consumption */
    printf("\n--- Thread Stack Allocations ---\n");
    printf("  %-4s %-16s %-12s %-12s\n", "PID", "Thread Name", "Stack Size", "Stack Used");
    printf("  --------------------------------------------------\n");

    for (int i = KERNEL_PID_FIRST; i <= KERNEL_PID_LAST; i++) {
        thread_t *t = (thread_t *)thread_get(i);
        if (t != NULL && t->name != NULL) {
            
            #ifdef DEVELHELP
            // Measure active usage by checking remaining spacing relative to stack start pointers
            int stack_free = thread_measure_stack_free(t);
            int total_stack = t->stack_size;
            int stack_used = total_stack - stack_free;

            printf("  %-4d %-16s %-12d %-12d\n", i, t->name, total_stack, stack_used);
             #else
            printf("  %-4d %-16s %-12d (Enable DEVELHELP to view stack use)\n", 
                   i, t->name, (int)t->stack_size);
            #endif
        }
    }
    printf("=============================================\n\n");

    return used_heap;
}

int main(void) {
    uint32_t test_runs = 5;

    uint32_t average_boot_times = boot_times(test_runs);

    thread_yield();
    
    ztimer_sleep(ZTIMER_MSEC, 2000);

    memory_footprint();

    printf("Allocating test memory block...\n");
    char *test_buffer = malloc(2048);
    if (test_buffer) {
        snprintf(test_buffer, 2048, "Simulated data payload string.");
    }

    memory_footprint();

    free(test_buffer);

    int cumulative_recovery = 0;
    int cumulative_memory = 0;
    uint32_t cumulative_cpu_percent = 0;
    uint32_t cumulative_cpu_fraction = 0;
    
    for (uint32_t i=0; i<test_runs; i++) {
        cumulative_recovery += sleep_recovery();
        cumulative_memory = memory_footprint();
        int* ptr = cpu_load();
        cumulative_cpu_percent += ptr[0];
        cumulative_cpu_fraction += ptr[1];
    }

    int average_recovery_times_us = cumulative_recovery / test_runs;
    int average_memory_footprint = cumulative_memory / test_runs;
    uint32_t average_recovery_times_ms = average_recovery_times_us / 1000;

    uint32_t average_cpu_percent = cumulative_cpu_percent / test_runs;
    uint32_t average_cpu_fraction = cumulative_cpu_fraction / test_runs;
    
    while(1) {
        printf("\n\nAverage Boot Times (us): %lu", (unsigned long)average_boot_times);
        printf("\nAverage Recovery Times (us): %lu", (unsigned long)average_recovery_times_us);
        printf("\nAverage Recovery Times (Approximate) (ms): %lu", (unsigned long)average_recovery_times_ms);
        printf("\nAverage CPU Load (%%): %lu.%lu", (unsigned long)average_cpu_percent, (unsigned long)average_cpu_fraction);
        printf("\nAverage Memory Footprint (bytes): %lu", (unsigned long)average_memory_footprint);
        ztimer_sleep(ZTIMER_MSEC, 5000);
    }

    return 0;
}