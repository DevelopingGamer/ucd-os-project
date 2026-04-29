//Include guard
#ifndef BENCHMARK_COMMON_H
#define BENCHMARK_COMMON_H

//Standard C library exact-width integers and booleans
#include <stdint.h>
#include <stdbool.h>

//Use -DFREERTOS_PICO, -DRIOT_OS, and -DZEPHYR compiler flags to compile 
// only for each operating system using the following headers
#if defined(FREERTOS_PICO)
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

//typedef struct to store the number of samples and the sum, minimum,
// maximum, average, and jitter (difference between max and min) of samples  
typedef struct {
    uint32_t samples;
    uint32_t sum;
    uint32_t min;
    uint32_t max;
    uint32_t avg;
    uint32_t jitter;
} stat_acc_t;

//Everytime a new measurement its value is added to the sum, the number
// of samples in incremented, and min and max are updated if applicable
static inline void stat_update(stat_acc_t *a, uint32_t val) {
    a->sum += val;
    a->samples++;
    if (val < a->min) a->min = val;
    if (val > a->max) a->max = val;
}

//At the end of the test the average and jitter is calculated 
static inline void stat_finalize(stat_acc_t *a) {
    if (a->samples > 0) a->avg = a->sum / a->samples;
    a->jitter = a->max - a->min;
}

//Macro used to initialize stat_acc_t by setting min to the highest
// possible value, and max, sum, and number of samples to 0 
#define STAT_INIT()  { .min = UINT32_MAX, .max = 0, .sum = 0, .samples = 0 }

//Struct which contains interrupt request latency, interrupt service
// routine to task latency, context switch latency, and the interrupt
// request latency when there is no higher priority task than the task
// that is calling the interrupt
typedef struct {
    stat_acc_t irq_latency_us;
    stat_acc_t irq_to_task_us;
    stat_acc_t ctx_switch_us;
    stat_acc_t load_latency_us;
} latency_results_t;

//Struct which contains the percent of time the CPU stays idle, the percent
// of time the CPU is busy, the number of ticks the CPU is idle for, and the
// total number of ticks
typedef struct {
    float     idle_pct;
    float     load_pct;
    uint32_t  idle_ticks;
    uint32_t  total_ticks;
} cpu_results_t;

//Get the amount of heap currently
// in use, free, the minimum amount of heap
// that has ever been free, the number of bytes
// in the current thread stack that is currently
// free, and heap_used_bytes in kilobytes
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

//Struct which contains maximum transfer speed, the percentage of packets
// lost, the maximum file write speed, the maximum file read speed, and the
// latency of the file sync command
typedef struct {
    uint32_t  uart_throughput_kbps;
    float     uart_packet_loss_pct;
    uint32_t  fs_write_kbps;
    uint32_t  fs_read_kbps;
    uint32_t  fs_sync_us;
} io_results_t;

//Struct which contains the number of task deadlines missed, the total number of
// tasks with deadlines that have run, the percentage of task deadlines missed,
// the number of priority inversions (bug where medium priority task preempts
// low priority priority task holding resource high priority task needs,
// preventing high priority task from getting that resource), the maximum number
// of tasks the OS can create before it fails, and the percentage of tasks that
// fail during the max tasks stress test
typedef struct {
    uint32_t  deadline_misses;
    uint32_t  deadline_total;
    float     miss_rate_pct;
    uint32_t  priority_inversion_cnt;
    uint32_t  max_tasks_created;
    float     task_failure_rate_pct;
} sched_results_t;

//Struct which contains the number of hours the system has been up for without a crash
// or reset, the number of milliseconds left in the current hour (ex. system has been 
// up for 1 hour and 10099 milliseconds without a crash), the recovery time after a
// failure, the number of failures during stress testing, and the number of interrupts
// per second during the stress test
typedef struct {
    uint32_t  uptime_hrs;
    uint32_t  uptime_ms_residual;
    uint32_t  recovery_ms;
    uint32_t  stress_fail_count;
    uint32_t  stress_irq_rate_hz;
} stability_results_t;

//Struct which records the number of sensor samples collected and processed, the number
// of samples the OS scheduler missed the deadline on, the sample miss rate percentage,
// the number of messages handed off to UART, the number of messages lost, the message
// loss rate percentage, the total number of tasks running while the sensor and network
// taks were running, the number of successful usages of the Interrupt Service Routine,
// and the number of failed usages of the Interrupt Service Routine
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

//Struct which records the boot time, the various metrics from the other structs, the
// number of times the benchmark tests should run, and the name of the operating system
// being tested
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

//Initializes shared hardware components needed by the benchmarking suite
void bench_init(void);
//Runs all tests in the suite
void bench_run_all(bench_results_t *out, uint32_t run_idx);
//Outputs results to serial output
void bench_log_results(const bench_results_t *r);
//Outputs csv header to store results as csv
void bench_log_csv_header(void);
//Outputs csv rows to store results as csv
void bench_log_csv_row(const bench_results_t *r);
//Prints debugging to serial output
void bench_trace_event(const char *event, const char *task, uint32_t pri);

//Include guard
#endif
