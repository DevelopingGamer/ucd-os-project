#ifndef BENCHMARK_COMMON_H
#define BENCHMARK_COMMON_H

#include <stdint.h>
#include <stdbool.h>

/* ── Platform detection ───────────────────────────────────────────── */
#if defined(FREERTOS)
  #include "FreeRTOS.h"
  #include "task.h"
  #include "semphr.h"
  #include "timers.h"
  #define RTOS_NAME         "FreeRTOS"
  #define TICK_TO_US(t)     ((t) * (1000000UL / configTICK_RATE_HZ))
  #define TICK_TO_MS(t)     ((t) * (1000UL    / configTICK_RATE_HZ))
  #define GET_TICK_RAW()    xTaskGetTickCount()
  typedef SemaphoreHandle_t bench_sem_t;

#elif defined(RIOT_OS)
  #include "thread.h"
  #include "mutex.h"
  #include "ztimer.h"
  #include "periph/timer.h"
  #define RTOS_NAME         "RIOT"
  #define GET_TICK_RAW()    ztimer_now(ZTIMER_USEC)
  typedef mutex_t           bench_sem_t;

#elif defined(ZEPHYR)
  #include <zephyr/kernel.h>
  #include <zephyr/timing/timing.h>
  #define RTOS_NAME         "Zephyr"
  #define GET_TICK_RAW()    k_uptime_get()
  typedef struct k_sem      bench_sem_t;
#endif

/* ── Statistical accumulator ──────────────────────────────────────── */
typedef struct {
    uint32_t samples;
    uint32_t sum;
    uint32_t min;
    uint32_t max;
    uint32_t avg;
    uint32_t jitter;
} stat_acc_t;

static inline void stat_update(stat_acc_t *a, uint32_t val) {
    a->sum += val;
    a->samples++;
    if (val < a->min) a->min = val;
    if (val > a->max) a->max = val;
}
static inline void stat_finalize(stat_acc_t *a) {
    if (a->samples > 0) a->avg = a->sum / a->samples;
    a->jitter = a->max - a->min;
}
#define STAT_INIT()  { .min = UINT32_MAX, .max = 0, .sum = 0, .samples = 0 }

/* ── Sub-structs ──────────────────────────────────────────────────── */
typedef struct {
    stat_acc_t irq_latency_us;
    stat_acc_t irq_to_task_us;
    stat_acc_t ctx_switch_us;
    stat_acc_t load_latency_us;
} latency_results_t;

typedef struct {
    float     idle_pct;
    float     load_pct;
    uint32_t  idle_ticks;
    uint32_t  total_ticks;
} cpu_results_t;

typedef struct {
    uint32_t  static_text_bytes;
    uint32_t  static_data_bytes;
    uint32_t  heap_used_bytes;
    uint32_t  heap_free_bytes;
    uint32_t  heap_total_bytes;
    uint32_t  heap_min_ever_free;
    uint32_t  task_stack_hwm_bytes;
    uint32_t  footprint_kb;
} mem_results_t;

typedef struct {
    uint32_t  uart_throughput_kbps;
    float     uart_packet_loss_pct;
    uint32_t  fs_write_kbps;
    uint32_t  fs_read_kbps;
    uint32_t  fs_sync_us;
} io_results_t;

typedef struct {
    uint32_t  deadline_misses;
    uint32_t  deadline_total;
    float     miss_rate_pct;
    uint32_t  priority_inversion_cnt;
    uint32_t  max_tasks_created;
    float     task_failure_rate_pct;
} sched_results_t;

typedef struct {
    uint32_t  uptime_hrs;
    uint32_t  uptime_ms_residual;
    uint32_t  recovery_ms;
    uint32_t  stress_fail_count;
    uint32_t  stress_irq_rate_hz;
} stability_results_t;

typedef struct {
    uint32_t  sensor_samples_total;
    uint32_t  sensor_missed_samples;
    float     sensor_miss_rate_pct;
    uint32_t  net_msgs_sent;
    uint32_t  net_msgs_lost;
    float     net_loss_pct;
    uint32_t  concurrent_tasks_run;
    uint32_t  hf_irq_count;
    uint32_t  hf_irq_miss_count;
} workload_results_t;

/* ── Master results structure ─────────────────────────────────────── */
typedef struct {
    uint32_t            boot_time_ms;
    latency_results_t   latency;
    cpu_results_t       cpu;
    mem_results_t       mem;
    io_results_t        io;
    sched_results_t     sched;
    stability_results_t stability;
    workload_results_t  workload;
    uint32_t            run_index;
    char                platform[16];
} bench_results_t;

/* ── Function declarations ────────────────────────────────────────── */
void bench_init(void);
void bench_run_all(bench_results_t *out, uint32_t run_idx);
void bench_log_results(const bench_results_t *r);
void bench_log_csv_header(void);
void bench_log_csv_row(const bench_results_t *r);
void bench_trace_event(const char *event, const char *task, uint32_t pri);

#endif /* BENCHMARK_COMMON_H */
