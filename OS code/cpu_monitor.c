/*
 * cpu_monitor.c
 *
 * KPIs covered:
 *   - CPU utilisation under idle conditions
 *   - CPU utilisation under load conditions
 *
 * Method:
 *   FreeRTOS: idle hook increments a counter; calibrated during a
 *             100-tick idle window at startup. Load = 100 - idle%.
 *   RIOT:     sched_statistics module accumulates per-thread runtime
 *             ticks; idle thread share gives idle%.
 *   Zephyr:   CONFIG_SCHED_THREAD_USAGE_ALL samples execution cycles
 *             from k_thread_runtime_stats_all_get().
 *
 * FreeRTOSConfig.h additions required:
 *   #define configUSE_IDLE_HOOK           1
 *   #define configUSE_TICK_HOOK           1
 *   #define configGENERATE_RUN_TIME_STATS 1
 *
 * RIOT Makefile addition:
 *   USEMODULE += schedstatistics
 *
 * Zephyr prj.conf additions:
 *   CONFIG_SCHED_THREAD_USAGE=y
 *   CONFIG_SCHED_THREAD_USAGE_ALL=y
 *   CONFIG_THREAD_ANALYZER=y
 */

#include "benchmark_common.h"
#include "cpu_monitor.h"

static volatile uint32_t _idle_counter = 0;
static volatile uint32_t _total_ticks  = 0;
static          uint32_t _idle_cal_max = 0;
static          bool     _cal_done     = false;

/* ════════════════════════════════════════════════════════════════════
 *  FreeRTOS
 * ═══════════════════════════════════════════════════════════════════*/
#if defined(FREERTOS)

void vApplicationIdleHook(void) { _idle_counter++; }
void vApplicationTickHook(void) { _total_ticks++;  }

static void _calibrate_idle(void) {
    uint32_t t0   = xTaskGetTickCount();
    uint32_t cnt0 = _idle_counter;
    while ((xTaskGetTickCount() - t0) < 100U) { taskYIELD(); }
    uint32_t delta = _idle_counter - cnt0;
    _idle_cal_max  = delta * (configTICK_RATE_HZ / 100U);
    _cal_done      = true;
}

cpu_results_t cpu_monitor_sample(void) {
    if (!_cal_done) _calibrate_idle();
    uint32_t tick_snap = _total_ticks;
    uint32_t i0        = _idle_counter;
    vTaskDelay(pdMS_TO_TICKS(1000U));
    uint32_t idle_delta = _idle_counter - i0;

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

/* ════════════════════════════════════════════════════════════════════
 *  RIOT OS
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(RIOT_OS)
#include "sched.h"

cpu_results_t cpu_monitor_sample(void) {
    cpu_results_t r = {0};
#ifdef MODULE_SCHEDSTATISTICS
    uint32_t idle0 = 0, total0 = 0;
    for (int i = 0; i < MAXTHREADS; i++) {
        thread_t *t = (thread_t *)sched_threads[i];
        if (!t) continue;
        total0 += t->schedstats.runtime_ticks;
        if (t->priority == THREAD_PRIORITY_IDLE)
            idle0 += t->schedstats.runtime_ticks;
    }
    ztimer_sleep(ZTIMER_MSEC, 1000U);
    uint32_t idle1 = 0, total1 = 0;
    for (int i = 0; i < MAXTHREADS; i++) {
        thread_t *t = (thread_t *)sched_threads[i];
        if (!t) continue;
        total1 += t->schedstats.runtime_ticks;
        if (t->priority == THREAD_PRIORITY_IDLE)
            idle1 += t->schedstats.runtime_ticks;
    }
    uint32_t d_idle  = idle1  - idle0;
    uint32_t d_total = total1 - total0;
    r.idle_ticks  = d_idle;
    r.total_ticks = d_total;
    r.idle_pct    = (d_total > 0)
                    ? (float)d_idle * 100.0f / (float)d_total : 0.0f;
    r.load_pct    = 100.0f - r.idle_pct;
#endif
    return r;
}

/* ════════════════════════════════════════════════════════════════════
 *  Zephyr
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(ZEPHYR)

cpu_results_t cpu_monitor_sample(void) {
    cpu_results_t r = {0};
#ifdef CONFIG_SCHED_THREAD_USAGE
    struct k_thread_runtime_stats idle0, total0;
    k_thread_runtime_stats_get(&_idle_thread, &idle0);
    k_thread_runtime_stats_all_get(&total0);
    k_msleep(1000U);
    struct k_thread_runtime_stats idle1, total1;
    k_thread_runtime_stats_get(&_idle_thread, &idle1);
    k_thread_runtime_stats_all_get(&total1);
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
