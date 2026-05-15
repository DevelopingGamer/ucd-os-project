//Headers included
#include "benchmark_common.h"
#include "cpu_monitor.h"

#if defined(FREERTOS_PICO)
//Initialize variables for the number of times the idle
// task runs, the total number of ticks, the total number
// of times the idle task can run in a second, and a boolean
// that determines whether or not idle_cal_max has been calculated
// yet or not
static volatile uint32_t _idle_counter = 0;
static volatile uint32_t _total_ticks  = 0;
static          uint32_t _idle_cal_max = 0;
static          bool     _cal_done     = false;

//Increment idle counter everytime the idle task runs
void vApplicationIdleHook(void) { _idle_counter++; }
//Have tick counter increment every tick
void vApplicationTickHook(void) { _total_ticks++;  }

static void _calibrate_idle(void) {
    //Get the current time
    uint32_t t0   = xTaskGetTickCount();
    //Get the current idle count
    uint32_t cnt0 = _idle_counter;
    //Sleep so that the idle task can run for 100 milliseconds
    vTaskDelay(pdMS_TO_TICKS(100));
    //Get the difference between the current idle count and the idle count
    // from before yielding the CPU for 100 milliseconds
    uint32_t delta = _idle_counter - cnt0;
    //Convert delta to seconds
    _idle_cal_max  = delta * (configTICK_RATE_HZ / 10);
    //Mark as calibrated
    _cal_done      = true;
}

cpu_results_t cpu_monitor_sample(void) {
    //Initialize all struct values to 0
    cpu_results_t r = {0};
    //Calibrate idle if not done
    if (!_cal_done) _calibrate_idle();
    
    //Get the current idle count
    uint32_t cnt0 = _idle_counter;
    //Sleep for 1 second
    vTaskDelay(pdMS_TO_TICKS(1000));
    //Get the difference between the current idle count and the idle count
    // from before yielding the CPU for 1 second
    uint32_t d_idle = _idle_counter - cnt0;

    //Populate metrics and return
    r.idle_ticks  = d_idle;
    r.total_ticks = _idle_cal_max;
    r.idle_pct    = (_idle_cal_max > 0)
                    ? (float)d_idle * 100.0f / (float)_idle_cal_max : 0.0f;
    r.load_pct    = 100.0f - r.idle_pct;

    return r;
}

#elif defined(RIOT_OS)
#include "schedstatistics.h"
#include "thread.h"
#include "ztimer.h"

cpu_results_t cpu_monitor_sample(void) {
    //Initialize all struct values to 0
    cpu_results_t r = {0};
#ifdef MODULE_SCHEDSTATISTICS
    //Initialize variables for idle count and total count
    uint64_t idle0 = 0, total0 = 0;
    uint64_t idle1 = 0, total1 = 0;

    //Get the current stats for all threads
    schedstat_t *stats = schedstat_get();
    //Iterate through all threads
    for (int i = 0; i < KERNEL_PID_LAST; i++) {
        //If the thread is active, add its runtime to total0, if it is the
        // idle thread, add its runtime to idle0
        if (thread_get(i)) {
            total0 += stats[i].runtime_ticks;
            if (i == 0) idle0 = stats[i].runtime_ticks;
        }
    }
    
    //Sleep for 1 second
    ztimer_sleep(ZTIMER_MSEC, 1000);
    
    //Get the current stats for all threads
    stats = schedstat_get();
    //Iterate through all threads
    for (int i = 0; i < KERNEL_PID_LAST; i++) {
        //If the thread is active, add its runtime to total1, if it is the
        // idle thread, add its runtime to idle1
        if (thread_get(i)) {
            total1 += stats[i].runtime_ticks;
            if (i == 0) idle1 = stats[i].runtime_ticks;
        }
    }

    //Get the difference between the current idle count and the idle count
    // from before yielding the CPU for 1 second
    uint32_t d_idle  = (uint32_t)(idle1 - idle0);
    //Get the difference between the current total count and the total count
    // from before yielding the CPU for 1 second
    uint32_t d_total = (uint32_t)(total1 - total0);
    //Populate metrics and return
    r.idle_ticks  = d_idle;
    r.total_ticks = d_total;
    r.idle_pct    = (d_total > 0)
                    ? (float)d_idle * 100.0f / (float)d_total : 0.0f;
    r.load_pct    = 100.0f - r.idle_pct;
#endif
    return r;
}

#elif defined(ZEPHYR)
#include <zephyr/kernel.h>
#include <zephyr/debug/stack.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel/thread_stack.h>
#include <zephyr/kernel/stats.h>

cpu_results_t cpu_monitor_sample(void) {
    cpu_results_t r = {0};

#if defined(CONFIG_SCHED_THREAD_USAGE) && defined(CONFIG_SCHED_THREAD_USAGE_ALL)
    struct k_thread_runtime_stats cpu_stats;
    
    // Grabs cumulative CPU execution data, including idle and total cycles
    k_thread_runtime_stats_cpu_get(0, &cpu_stats);

    uint32_t total = (uint32_t)cpu_stats.total_cycles;
    uint32_t idle  = (uint32_t)cpu_stats.idle_cycles;

    r.total_ticks = total;
    r.idle_ticks  = idle;
    
    r.idle_pct = (total > 0) ? ((float)idle * 100.0f) / (float)total : 0.0f;
    
#endif
    return r;
}

#endif