/*
 * bench_log.c
 *
 * Data collection methods implemented here:
 *   - Serial logging: human-readable UART output for all runtime KPIs
 *   - Kernel trace:   [TRACE] tagged lines parseable by grep/logic analyser
 *   - CSV output:     machine-readable rows for Python/Excel post-processing
 *                     across repeated runs (statistical consistency)
 *
 * Capture serial output:
 *   screen /dev/ttyUSB0 115200 | tee run_log.txt
 *
 * Extract CSV only:
 *   grep -v "^\[" run_log.txt > results.csv
 *
 * Extract kernel trace only:
 *   grep "^\[TRACE\]" run_log.txt > trace.log
 */


//Included headers
#include "benchmark_common.h"
#include "bench_log.h"
//Included libraries
#include <stdio.h>
#include <string.h>

//Create macros for print for each operating system so that most of the below 
// code can work on all three operating systems
#if defined(FREERTOS_PICO)
  #define LOG_PRINT(...)  printf(__VA_ARGS__)
#elif defined(RIOT_OS)
  #define LOG_PRINT(...)  printf(__VA_ARGS__)
#elif defined(ZEPHYR)
  #include <zephyr/sys/printk.h>
  #define LOG_PRINT(...)  printk(__VA_ARGS__)
#endif

//Get the start and end of the .text, .data, and .bss sections of the operating system
// and the compiled code to calculate its size
//Get the start and end of the .text, .data, and .bss sections of the operating system
// and the compiled code to calculate its size
#if defined(FREERTOS_PICO)
  // Pico SDK uses custom linker scripts, bypass static tracking for this port
  #define STATIC_TEXT_BYTES  0UL
  #define STATIC_DATA_BYTES  0UL
#elif defined(RIOT_OS)
  extern uint32_t _stext, _etext, _sdata, _edata, _sbss, _ebss;
  #define STATIC_TEXT_BYTES  ((uint32_t)(&_etext - &_stext))
  #define STATIC_DATA_BYTES  ((uint32_t)((&_edata - &_sdata) + (&_ebss - &_sbss)))
#elif defined(ZEPHYR)
  extern char __text_region_start, __text_region_end;
  extern char __bss_start, _end;
  #define STATIC_TEXT_BYTES  ((uint32_t)(&__text_region_end - &__text_region_start))
  #define STATIC_DATA_BYTES  ((uint32_t)(&_end - &__bss_start))
#endif

/* ════════════════════════════════════════════════════════════════════
 *  Human-readable serial report — all KPIs
 * ═══════════════════════════════════════════════════════════════════*/
void bench_log_results(const bench_results_t *r) {
    LOG_PRINT("\r\n");
    LOG_PRINT("====================================================\r\n");
    LOG_PRINT("  RTOS Benchmark: %-16s  Run %lu\r\n",
              r->platform, r->run_index);
    LOG_PRINT("====================================================\r\n");

    /* Boot & recovery */
    LOG_PRINT("\r\n[BOOT & RECOVERY]\r\n");
    LOG_PRINT("  Boot time               : %lu ms\r\n",  r->boot_time_ms);
    LOG_PRINT("  System recovery time    : %lu ms\r\n",  r->stability.recovery_ms);

    /* CPU utilisation */
    LOG_PRINT("\r\n[CPU UTILISATION]\r\n");
    LOG_PRINT("  Idle                    : %.2f %%\r\n", r->cpu.idle_pct);
    LOG_PRINT("  Under load              : %.2f %%\r\n", r->cpu.load_pct);
    LOG_PRINT("  Idle ticks              : %lu\r\n",     r->cpu.idle_ticks);
    LOG_PRINT("  Total ticks             : %lu\r\n",     r->cpu.total_ticks);

    /* Memory - compile-time */
    LOG_PRINT("\r\n[MEMORY FOOTPRINT - COMPILE TIME]\r\n");
    LOG_PRINT("  .text + .rodata         : %lu bytes\r\n", STATIC_TEXT_BYTES);
    LOG_PRINT("  .data + .bss            : %lu bytes\r\n", STATIC_DATA_BYTES);

    /* Memory - runtime */
    LOG_PRINT("\r\n[MEMORY FOOTPRINT - RUNTIME]\r\n");
    LOG_PRINT("  Heap used               : %lu bytes\r\n", r->mem.heap_used_bytes);
    LOG_PRINT("  Heap free               : %lu bytes\r\n", r->mem.heap_free_bytes);
    LOG_PRINT("  Heap low-water mark     : %lu bytes\r\n", r->mem.heap_min_ever_free);
    LOG_PRINT("  Task stack HWM          : %lu bytes\r\n", r->mem.task_stack_hwm_bytes);
    LOG_PRINT("  Total footprint         : %lu KB\r\n",    r->mem.footprint_kb);

    /* Interrupt latency & response time */
    LOG_PRINT("\r\n[INTERRUPT LATENCY & RESPONSE TIME]\r\n");
    LOG_PRINT("  IRQ latency  avg        : %lu us\r\n",
              r->latency.irq_latency_us.avg);
    LOG_PRINT("  IRQ latency  min        : %lu us\r\n",
              r->latency.irq_latency_us.min);
    LOG_PRINT("  IRQ latency  max        : %lu us\r\n",
              r->latency.irq_latency_us.max);
    LOG_PRINT("  IRQ latency  jitter     : %lu us\r\n",
              r->latency.irq_latency_us.jitter);
    LOG_PRINT("  IRQ -> task  avg        : %lu us\r\n",
              r->latency.irq_to_task_us.avg);
    LOG_PRINT("  IRQ -> task  jitter     : %lu us\r\n",
              r->latency.irq_to_task_us.jitter);
    LOG_PRINT("  Latency under load avg  : %lu us\r\n",
              r->latency.load_latency_us.avg);

    /* Scheduling efficiency & context switch */
    LOG_PRINT("\r\n[SCHEDULING EFFICIENCY & CONTEXT SWITCH]\r\n");
    LOG_PRINT("  Context switch avg      : %lu us\r\n",
              r->latency.ctx_switch_us.avg);
    LOG_PRINT("  Context switch min      : %lu us\r\n",
              r->latency.ctx_switch_us.min);
    LOG_PRINT("  Context switch jitter   : %lu us\r\n",
              r->latency.ctx_switch_us.jitter);
    LOG_PRINT("  Deadline misses         : %lu / %lu  (%.2f %%)\r\n",
              r->sched.deadline_misses,
              r->sched.deadline_total,
              r->sched.miss_rate_pct);
    LOG_PRINT("  Priority inversions     : %lu\r\n",
              r->sched.priority_inversion_cnt);
    LOG_PRINT("  Max concurrent tasks    : %lu\r\n",
              r->sched.max_tasks_created);
    LOG_PRINT("  Task failure rate       : %.2f %%\r\n",
              r->sched.task_failure_rate_pct);

    /* I/O throughput & filesystem */
    LOG_PRINT("\r\n[I/O THROUGHPUT & FILESYSTEM]\r\n");
    LOG_PRINT("  UART throughput         : %lu KB/s\r\n",
              r->io.uart_throughput_kbps);
    LOG_PRINT("  UART packet loss        : %.2f %%\r\n",
              r->io.uart_packet_loss_pct);
    LOG_PRINT("  FS write throughput     : %lu KB/s\r\n",
              r->io.fs_write_kbps);
    LOG_PRINT("  FS read  throughput     : %lu KB/s\r\n",
              r->io.fs_read_kbps);
    LOG_PRINT("  FS sync latency         : %lu us\r\n",
              r->io.fs_sync_us);

    /* Long-term stability */
    LOG_PRINT("\r\n[LONG-TERM STABILITY]\r\n");
    LOG_PRINT("  Uptime                  : %lu hrs\r\n",
              r->stability.uptime_hrs);
    LOG_PRINT("  Stress failures         : %lu\r\n",
              r->stability.stress_fail_count);
    LOG_PRINT("  HF IRQ rate             : %lu Hz\r\n",
              r->stability.stress_irq_rate_hz);

    /* Workload simulation */
    LOG_PRINT("\r\n[WORKLOAD SIMULATION]\r\n");
    LOG_PRINT("  Sensor: missed/total    : %lu / %lu  (%.2f %%)\r\n",
              r->workload.sensor_missed_samples,
              r->workload.sensor_samples_total,
              r->workload.sensor_miss_rate_pct);
    LOG_PRINT("  Net: sent/lost          : %lu / %lu  (%.2f %%)\r\n",
              r->workload.net_msgs_sent,
              r->workload.net_msgs_lost,
              r->workload.net_loss_pct);
    LOG_PRINT("  Concurrent tasks run    : %lu\r\n",
              r->workload.concurrent_tasks_run);
    LOG_PRINT("  HF IRQ count / missed   : %lu / %lu\r\n",
              r->workload.hf_irq_count,
              r->workload.hf_irq_miss_count);
    LOG_PRINT("\r\n");
}


//Serial print csv output 
/* ════════════════════════════════════════════════════════════════════
 *  CSV header — one call at program start
 * ═══════════════════════════════════════════════════════════════════*/
void bench_log_csv_header(void) {
    LOG_PRINT("platform,run,"
              "boot_ms,recovery_ms,"
              "cpu_idle_pct,cpu_load_pct,"
              "mem_text_b,mem_data_b,"
              "heap_used_b,heap_free_b,footprint_kb,"
              "irq_avg_us,irq_min_us,irq_max_us,irq_jitter_us,"
              "irq_to_task_avg_us,irq_to_task_jitter_us,"
              "ctx_sw_avg_us,ctx_sw_jitter_us,"
              "load_lat_avg_us,"
              "deadline_miss_pct,priority_inv,max_tasks,fail_rate_pct,"
              "uart_kbps,uart_loss_pct,"
              "fs_write_kbps,fs_read_kbps,fs_sync_us,"
              "uptime_hrs,stress_fails,"
              "sensor_miss_pct,net_loss_pct,hf_irq_cnt,hf_miss_cnt"
              "\r\n");
}

/* ════════════════════════════════════════════════════════════════════
 *  CSV data row — one call per run
 * ═══════════════════════════════════════════════════════════════════*/
void bench_log_csv_row(const bench_results_t *r) {
    LOG_PRINT("%s,%lu,"
              "%lu,%lu,"
              "%.2f,%.2f,"
              "%lu,%lu,"
              "%lu,%lu,%lu,"
              "%lu,%lu,%lu,%lu,"
              "%lu,%lu,"
              "%lu,%lu,"
              "%lu,"
              "%.2f,%lu,%lu,%.2f,"
              "%lu,%.2f,"
              "%lu,%lu,%lu,"
              "%lu,%lu,"
              "%.2f,%.2f,%lu,%lu"
              "\r\n",
              r->platform, r->run_index,
              r->boot_time_ms, r->stability.recovery_ms,
              r->cpu.idle_pct, r->cpu.load_pct,
              STATIC_TEXT_BYTES, STATIC_DATA_BYTES,
              r->mem.heap_used_bytes, r->mem.heap_free_bytes,
              r->mem.footprint_kb,
              r->latency.irq_latency_us.avg,
              r->latency.irq_latency_us.min,
              r->latency.irq_latency_us.max,
              r->latency.irq_latency_us.jitter,
              r->latency.irq_to_task_us.avg,
              r->latency.irq_to_task_us.jitter,
              r->latency.ctx_switch_us.avg,
              r->latency.ctx_switch_us.jitter,
              r->latency.load_latency_us.avg,
              r->sched.miss_rate_pct,
              r->sched.priority_inversion_cnt,
              r->sched.max_tasks_created,
              r->sched.task_failure_rate_pct,
              r->io.uart_throughput_kbps,
              r->io.uart_packet_loss_pct,
              r->io.fs_write_kbps,
              r->io.fs_read_kbps,
              r->io.fs_sync_us,
              r->stability.uptime_hrs,
              r->stability.stress_fail_count,
              r->workload.sensor_miss_rate_pct,
              r->workload.net_loss_pct,
              r->workload.hf_irq_count,
              r->workload.hf_irq_miss_count);
}

/* ════════════════════════════════════════════════════════════════════
 *  Kernel trace output — scheduling analysis
 *  Format: [TRACE] <ts_us> <event> <task> <priority>
 *  Parse:  grep "^\[TRACE\]" run_log.txt > trace.log
 * ═══════════════════════════════════════════════════════════════════*/
void bench_trace_event(const char *event, const char *task, uint32_t pri) {
#if defined(FREERTOS_PICO)
    uint32_t ts = TICK_TO_US(xTaskGetTickCount());
#elif defined(RIOT_OS)
    uint32_t ts = ztimer_now(ZTIMER_USEC);
#elif defined(ZEPHYR)
    uint32_t ts = (uint32_t)k_uptime_get() * 1000U;
#endif
    LOG_PRINT("[TRACE] %lu %s %s %lu\r\n", ts, event, task, pri);
}
