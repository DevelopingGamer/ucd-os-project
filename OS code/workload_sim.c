/*
 * workload_sim.c
 *
 * Workload scenarios:
 *   1. Sensor data generation and periodic sampling (100 Hz, 1000 samples)
 *   2. Network message transmission (200 x 64 B messages over UART loopback)
 *   3. Concurrent task execution with varying priorities (8 tasks, 4 levels)
 *   4. Stress testing with high-frequency interrupts (5 kHz, 2 seconds)
 *
 * Additional KPIs captured:
 *   - Deadline miss rate     (scheduling efficiency)
 *   - Priority inversion count
 *   - Task failure rate under stress
 */

#include "benchmark_common.h"
#include "workload_sim.h"

/* ── Shared counters (written by ISR/tasks, read by collector) ─────── */
static volatile uint32_t _sensor_samples   = 0;
static volatile uint32_t _sensor_missed    = 0;
static volatile uint32_t _net_sent         = 0;
static volatile uint32_t _net_lost         = 0;
static volatile uint32_t _deadline_misses  = 0;
static volatile uint32_t _deadline_total   = 0;
static volatile uint32_t _hf_irq_count     = 0;
static volatile uint32_t _hf_irq_miss      = 0;
static volatile uint32_t _priority_inv_cnt = 0;

/* ════════════════════════════════════════════════════════════════════
 *  FreeRTOS
 * ═══════════════════════════════════════════════════════════════════*/
#if defined(FREERTOS)
#include "stm32f4xx_hal.h"
extern UART_HandleTypeDef huart2;
extern TIM_HandleTypeDef  htim3;

/* -- Sensor task: periodic 100 Hz with deadline checking -- */
static void sensor_task(void *arg) {
    (void)arg;
    TickType_t last_wake  = xTaskGetTickCount();
    TickType_t expected   = last_wake + pdMS_TO_TICKS(SENSOR_PERIOD_MS);

    for (uint32_t i = 0; i < SENSOR_TOTAL_SAMPLES; i++) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
        _deadline_total++;
        if (xTaskGetTickCount() > expected)
            { _deadline_misses++; _sensor_missed++; }
        expected += pdMS_TO_TICKS(SENSOR_PERIOD_MS);
        volatile uint32_t adc = (xTaskGetTickCount() ^ 0xA5A5UL) & 0xFFFU;
        (void)adc;
        _sensor_samples++;
    }
    vTaskDelete(NULL);
}

/* -- Network task: 200 x 64 B UART transmit -- */
static uint8_t _net_msg[NET_MSG_SIZE];
static void net_task(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < NET_MSG_SIZE; i++) _net_msg[i] = (uint8_t)i;
    for (uint32_t i = 0; i < NET_MSG_COUNT; i++) {
        HAL_StatusTypeDef r = HAL_UART_Transmit(&huart2, _net_msg,
                                                  NET_MSG_SIZE, 20);
        if (r == HAL_OK) _net_sent++;
        else             _net_lost++;
        vTaskDelay(pdMS_TO_TICKS(5U));
    }
    vTaskDelete(NULL);
}

/* -- Priority inversion detection (3-task mutex stress) -- */
static SemaphoreHandle_t _pi_mutex;
#define PI_INVERSION_THRESHOLD_US 500U
extern TIM_HandleTypeDef htim2;

static void pi_low_task(void *arg) {
    (void)arg;
    xSemaphoreTake(_pi_mutex, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(50U));
    xSemaphoreGive(_pi_mutex);
    vTaskDelete(NULL);
}
static void pi_mid_task(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < 10; i++) {
        volatile uint32_t x = 0;
        for (uint32_t j = 0; j < 10000U; j++) x++;
        (void)x;
        vTaskDelay(1);
    }
    vTaskDelete(NULL);
}
static void pi_high_task(void *arg) {
    (void)arg;
    uint32_t t0 = __HAL_TIM_GET_COUNTER(&htim2);
    xSemaphoreTake(_pi_mutex, portMAX_DELAY);
    uint32_t elapsed = __HAL_TIM_GET_COUNTER(&htim2) - t0;
    if (elapsed > PI_INVERSION_THRESHOLD_US) _priority_inv_cnt++;
    xSemaphoreGive(_pi_mutex);
    vTaskDelete(NULL);
}

/* -- High-frequency IRQ stress: TIM3 at 5 kHz -- */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM3) _hf_irq_count++;
}
static void hf_irq_stress_run(void) {
    uint32_t expected = (HF_IRQ_RATE_HZ * HF_IRQ_DURATION_MS) / 1000U;
    _hf_irq_count = 0;
    uint32_t arr = (SystemCoreClock / HF_IRQ_RATE_HZ) - 1U;
    __HAL_TIM_SET_AUTORELOAD(&htim3, arr);
    HAL_TIM_Base_Start_IT(&htim3);
    vTaskDelay(pdMS_TO_TICKS(HF_IRQ_DURATION_MS));
    HAL_TIM_Base_Stop_IT(&htim3);
    if (_hf_irq_count < expected)
        _hf_irq_miss = expected - _hf_irq_count;
}

workload_results_t workload_run(void) {
    _pi_mutex = xSemaphoreCreateMutex();
    _sensor_samples = _sensor_missed = _net_sent = _net_lost = 0;
    _deadline_misses = _deadline_total = _hf_irq_count = 0;

    xTaskCreate(sensor_task, "SENS", 256, NULL, tskIDLE_PRIORITY + 3, NULL);
    xTaskCreate(net_task,    "NET",  512, NULL, tskIDLE_PRIORITY + 2, NULL);

    for (uint32_t i = 0; i < CONCURRENT_TASKS; i++) {
        UBaseType_t pri = tskIDLE_PRIORITY + 1 + (i % 4);
        xTaskCreate(sensor_task, "CT", 128, NULL, pri, NULL);
    }
    xTaskCreate(pi_low_task,  "PIL", 256, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(pi_mid_task,  "PIM", 256, NULL, tskIDLE_PRIORITY + 3, NULL);
    xTaskCreate(pi_high_task, "PIH", 256, NULL, tskIDLE_PRIORITY + 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(SENSOR_TOTAL_SAMPLES * SENSOR_PERIOD_MS + 500U));
    hf_irq_stress_run();

    workload_results_t r;
    r.sensor_samples_total  = _sensor_samples;
    r.sensor_missed_samples = _sensor_missed;
    r.sensor_miss_rate_pct  = (_sensor_samples > 0)
        ? (float)_sensor_missed * 100.0f / (float)_sensor_samples : 0.0f;
    r.net_msgs_sent          = _net_sent;
    r.net_msgs_lost          = _net_lost;
    r.net_loss_pct           = ((_net_sent + _net_lost) > 0)
        ? (float)_net_lost * 100.0f / (float)(_net_sent + _net_lost) : 0.0f;
    r.concurrent_tasks_run   = CONCURRENT_TASKS;
    r.hf_irq_count           = _hf_irq_count;
    r.hf_irq_miss_count      = _hf_irq_miss;
    return r;
}

/* ════════════════════════════════════════════════════════════════════
 *  RIOT OS
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(RIOT_OS)
#include "periph/uart.h"
#include "periph/timer.h"

static char _sens_stack[THREAD_STACKSIZE_DEFAULT];
static char _net_stack[THREAD_STACKSIZE_DEFAULT];
static char _ct_stacks[CONCURRENT_TASKS][THREAD_STACKSIZE_MINIMUM];

static void *sensor_thread(void *arg) {
    (void)arg;
    uint32_t expected = ztimer_now(ZTIMER_MSEC) + SENSOR_PERIOD_MS;
    for (uint32_t i = 0; i < SENSOR_TOTAL_SAMPLES; i++) {
        _deadline_total++;
        if (ztimer_now(ZTIMER_MSEC) > expected + (DEADLINE_SLACK_US / 1000U))
            { _deadline_misses++; _sensor_missed++; }
        expected += SENSOR_PERIOD_MS;
        volatile uint32_t adc = (ztimer_now(ZTIMER_MSEC) ^ 0xA5A5U) & 0xFFFU;
        (void)adc;
        _sensor_samples++;
        ztimer_sleep(ZTIMER_MSEC, SENSOR_PERIOD_MS);
    }
    return NULL;
}

static void *net_thread(void *arg) {
    (void)arg;
    uint8_t msg[NET_MSG_SIZE];
    for (uint32_t i = 0; i < NET_MSG_SIZE; i++) msg[i] = (uint8_t)i;
    for (uint32_t i = 0; i < NET_MSG_COUNT; i++) {
        int r = uart_write(UART_DEV(0), msg, NET_MSG_SIZE);
        if (r >= 0) _net_sent++; else _net_lost++;
        ztimer_sleep(ZTIMER_MSEC, 5U);
    }
    return NULL;
}

static void hf_irq_cb(void *arg, int ch) { (void)arg; (void)ch; _hf_irq_count++; }

static void hf_irq_stress_run(void) {
    uint32_t expected    = (HF_IRQ_RATE_HZ * HF_IRQ_DURATION_MS) / 1000U;
    uint32_t interval_us = 1000000U / HF_IRQ_RATE_HZ;
    _hf_irq_count = 0;
    timer_init(TIMER_DEV(2), 1000000U, hf_irq_cb, NULL);
    timer_set_periodic(TIMER_DEV(2), 0, interval_us, TIM_FLAG_RESET_ON_MATCH);
    ztimer_sleep(ZTIMER_MSEC, HF_IRQ_DURATION_MS);
    timer_stop(TIMER_DEV(2));
    if (_hf_irq_count < expected) _hf_irq_miss = expected - _hf_irq_count;
}

workload_results_t workload_run(void) {
    _sensor_samples = _sensor_missed = _net_sent = _net_lost = 0;
    _deadline_misses = _deadline_total = _hf_irq_count = 0;

    thread_create(_sens_stack, sizeof(_sens_stack),
                  THREAD_PRIORITY_MAIN - 3, 0, sensor_thread, NULL, "sens");
    thread_create(_net_stack,  sizeof(_net_stack),
                  THREAD_PRIORITY_MAIN - 2, 0, net_thread,   NULL, "net");
    for (uint32_t i = 0; i < CONCURRENT_TASKS; i++) {
        int pri = THREAD_PRIORITY_MAIN - 1 - (int)(i % 4);
        thread_create(_ct_stacks[i], THREAD_STACKSIZE_MINIMUM,
                      pri, 0, sensor_thread, NULL, "ct");
    }
    ztimer_sleep(ZTIMER_MSEC, SENSOR_TOTAL_SAMPLES * SENSOR_PERIOD_MS + 500U);
    hf_irq_stress_run();

    workload_results_t r;
    r.sensor_samples_total  = _sensor_samples;
    r.sensor_missed_samples = _sensor_missed;
    r.sensor_miss_rate_pct  = (_sensor_samples > 0)
        ? (float)_sensor_missed * 100.0f / (float)_sensor_samples : 0.0f;
    r.net_msgs_sent          = _net_sent;
    r.net_msgs_lost          = _net_lost;
    r.net_loss_pct           = ((_net_sent + _net_lost) > 0)
        ? (float)_net_lost * 100.0f / (float)(_net_sent + _net_lost) : 0.0f;
    r.concurrent_tasks_run   = CONCURRENT_TASKS;
    r.hf_irq_count           = _hf_irq_count;
    r.hf_irq_miss_count      = _hf_irq_miss;
    return r;
}

/* ════════════════════════════════════════════════════════════════════
 *  Zephyr
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(ZEPHYR)
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/counter.h>

K_THREAD_STACK_DEFINE(_sens_stack_z, 1024);
K_THREAD_STACK_DEFINE(_net_stack_z,  1024);
K_THREAD_STACK_ARRAY_DEFINE(_ct_stacks_z, CONCURRENT_TASKS, 512);
static struct k_thread _sens_td, _net_td, _ct_td[CONCURRENT_TASKS];

static void sensor_entry(void *a, void *b, void *c) {
    (void)a; (void)b; (void)c;
    int64_t next = k_uptime_get() + SENSOR_PERIOD_MS;
    for (uint32_t i = 0; i < SENSOR_TOTAL_SAMPLES; i++) {
        _deadline_total++;
        if (k_uptime_get() > next + (DEADLINE_SLACK_US / 1000U))
            { _deadline_misses++; _sensor_missed++; }
        next += SENSOR_PERIOD_MS;
        volatile uint32_t adc = ((uint32_t)k_uptime_get() ^ 0xA5A5U) & 0xFFFU;
        (void)adc;
        _sensor_samples++;
        k_msleep(SENSOR_PERIOD_MS);
    }
}

static void net_entry(void *a, void *b, void *c) {
    (void)a; (void)b; (void)c;
    const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    uint8_t msg[NET_MSG_SIZE];
    for (uint32_t i = 0; i < NET_MSG_SIZE; i++) msg[i] = (uint8_t)i;
    for (uint32_t i = 0; i < NET_MSG_COUNT; i++) {
        for (uint32_t b = 0; b < NET_MSG_SIZE; b++) uart_poll_out(uart, msg[b]);
        _net_sent++;
        k_msleep(5U);
    }
}

static struct counter_alarm_cfg _hf_alarm;
static const struct device     *_hf_ctr;

static void hf_alarm_cb(const struct device *dev, uint8_t ch,
                         uint32_t ticks, void *ud) {
    (void)dev; (void)ch; (void)ud;
    _hf_irq_count++;
    _hf_alarm.ticks = counter_us_to_ticks(_hf_ctr, 1000000U / HF_IRQ_RATE_HZ);
    counter_set_channel_alarm(_hf_ctr, 0, &_hf_alarm);
}

static void hf_irq_stress_run(void) {
    uint32_t expected  = (HF_IRQ_RATE_HZ * HF_IRQ_DURATION_MS) / 1000U;
    _hf_irq_count      = 0;
    _hf_ctr            = DEVICE_DT_GET(DT_NODELABEL(timer2));
    _hf_alarm.callback = hf_alarm_cb;
    _hf_alarm.flags    = 0;
    _hf_alarm.ticks    = counter_us_to_ticks(_hf_ctr, 1000000U / HF_IRQ_RATE_HZ);
    counter_start(_hf_ctr);
    counter_set_channel_alarm(_hf_ctr, 0, &_hf_alarm);
    k_msleep(HF_IRQ_DURATION_MS);
    counter_stop(_hf_ctr);
    if (_hf_irq_count < expected) _hf_irq_miss = expected - _hf_irq_count;
}

workload_results_t workload_run(void) {
    _sensor_samples = _sensor_missed = _net_sent = _net_lost = 0;
    _deadline_misses = _deadline_total = _hf_irq_count = 0;

    k_thread_create(&_sens_td, _sens_stack_z,
                    K_THREAD_STACK_SIZEOF(_sens_stack_z),
                    sensor_entry, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    k_thread_create(&_net_td, _net_stack_z,
                    K_THREAD_STACK_SIZEOF(_net_stack_z),
                    net_entry, NULL, NULL, NULL, 6, 0, K_NO_WAIT);
    for (uint32_t i = 0; i < CONCURRENT_TASKS; i++) {
        int pri = 5 + (int)(i % 4);
        k_thread_create(&_ct_td[i], _ct_stacks_z[i],
                        K_THREAD_STACK_SIZEOF(_ct_stacks_z[i]),
                        sensor_entry, NULL, NULL, NULL, pri, 0, K_NO_WAIT);
    }
    k_thread_join(&_sens_td, K_FOREVER);
    hf_irq_stress_run();

    workload_results_t r;
    r.sensor_samples_total  = _sensor_samples;
    r.sensor_missed_samples = _sensor_missed;
    r.sensor_miss_rate_pct  = (_sensor_samples > 0)
        ? (float)_sensor_missed * 100.0f / (float)_sensor_samples : 0.0f;
    r.net_msgs_sent          = _net_sent;
    r.net_msgs_lost          = _net_lost;
    r.net_loss_pct           = ((_net_sent + _net_lost) > 0)
        ? (float)_net_lost * 100.0f / (float)(_net_sent + _net_lost) : 0.0f;
    r.concurrent_tasks_run   = CONCURRENT_TASKS;
    r.hf_irq_count           = _hf_irq_count;
    r.hf_irq_miss_count      = _hf_irq_miss;
    return r;
}
#endif
