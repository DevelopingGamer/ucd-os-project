//Headers included
#include "benchmark_common.h"
#include "cpu_monitor.h"

//Initialize variables for the number of times the idle
// task runs, the total number of ticks, the total number
// of times the idle task can run in a second, and a boolean
// that determines whether or not idle_cal_max has been calculated
// yet or not
static volatile uint32_t _idle_counter = 0;
static volatile uint32_t _total_ticks  = 0;
static          uint32_t _idle_cal_max = 0;
static          bool     _cal_done     = false;

#if defined(FREERTOS_PICO)

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
    _idle_cal_max  = delta * (configTICK_RATE_HZ / 100U);
    //Set _cal_done to true to signified that _idle_cal_max has been calculated
    _cal_done      = true;
}

cpu_results_t cpu_monitor_sample(void) {
    //Calculate _idle_cal_max if it hasn't been calculated yet
    if (!_cal_done) _calibrate_idle();
    //Get the current number of ticks
    uint32_t tick_snap = _total_ticks;
    //Get the current number of times the idle task has ran
    uint32_t i0        = _idle_counter;
    //Sleep for a second
    vTaskDelay(pdMS_TO_TICKS(1000U));
    //Get the number of times the idle_counter incremented while
    // this process was asleep for a second
    uint32_t idle_delta = _idle_counter - i0;

    //Populate metrics struct with values from global variables and values
    // calculated from those variables and return them
    cpu_results_t r;
    r.total_ticks = _total_ticks - tick_snap;
    r.idle_ticks  = idle_delta;
    r.idle_pct    = (_idle_cal_max > 0)
                    ? (float)idle_delta * 100.0f / (float)_idle_cal_max
                    : 0.0f;
    if (r.idle_pct > 100.0f) r.idle_pct = 100.0f;
    r.load_pct    = 100.0f - r.idle_pct;
    return r;
}

#elif defined(RIOT_OS)
#include "sched.h"

cpu_results_t cpu_monitor_sample(void) {
    //Initialize struct values to 0
    cpu_results_t r = {0};
//Only compile if USEMODULE += schedstatistics is in makefile
#ifdef MODULE_SCHEDSTATISTICS
    //Initialize idle and total time before sleeping for a second to 0
    uint32_t idle0 = 0, total0 = 0;
    //Add all clock cycles consumed by all currently running threads
    // since the microcontroller booted to total, and add all threads
    // consumed by tasks with idle thread priority
    for (int i = 0; i < MAXTHREADS; i++) {
        thread_t *t = (thread_t *)sched_threads[i];
        if (!t) continue;
        total0 += t->schedstats.runtime_ticks;
        if (t->priority == THREAD_PRIORITY_IDLE)
            idle0 += t->schedstats.runtime_ticks;
    }
    //Sleep for 1 second
    ztimer_sleep(ZTIMER_MSEC, 1000U);
    //Initialize idle and total time after sleeping for a second to 1
    uint32_t idle1 = 0, total1 = 0;
    //Do the same calculations as before
    for (int i = 0; i < MAXTHREADS; i++) {
        thread_t *t = (thread_t *)sched_threads[i];
        if (!t) continue;
        total1 += t->schedstats.runtime_ticks;
        if (t->priority == THREAD_PRIORITY_IDLE)
            idle1 += t->schedstats.runtime_ticks;
    }
    //Get the delta for idle and total time
    uint32_t d_idle  = idle1  - idle0;
    uint32_t d_total = total1 - total0;
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

cpu_results_t cpu_monitor_sample(void) {
    //Initialize all struct values to 0
    cpu_results_t r = {0};
//Only compile if CONFIG_SCHED_THREAD_USAGE=y, 
// CONFIG_SCHED_THREAD_USAGE_ALL=y, and 
// CONFIG_THREAD_ANALYZER=y are in prj.conf
#ifdef CONFIG_SCHED_THREAD_USAGE
    //Get usage stats for idle thread and all threads
    struct k_thread_runtime_stats idle0, total0;
    k_thread_runtime_stats_get(&_idle_thread, &idle0);
    k_thread_runtime_stats_all_get(&total0);
    //Sleep for 1 second
    k_msleep(1000U);
    //Get usage stats for idle thread and all threads again
    struct k_thread_runtime_stats idle1, total1;
    k_thread_runtime_stats_get(&_idle_thread, &idle1);
    k_thread_runtime_stats_all_get(&total1);
    //Calculate delta, populate metrics struct, and return the metrics struct
    uint64_t d_idle  = idle1.execution_cycles  - idle0.execution_cycles;
    uint64_t d_total = total1.execution_cycles - total0.execution_cycles;
    r.idle_ticks  = (uint32_t)d_idle;
    r.total_ticks = (uint32_t)d_total;
    r.idle_pct    = (d_total > 0)
                    ? (float)d_idle * 100.0f / (float)d_total : 0.0f;
    r.load_pct    = 100.0f - r.idle_pct;
#endif
    return r;
}
#endif
