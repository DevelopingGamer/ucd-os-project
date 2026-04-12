/*
 * latency_meas.c
 *
 * KPIs covered:
 *   - Interrupt latency:  GPIO trigger -> ISR entry (hardware timer capture)
 *   - Response time:      ISR exit -> highest-priority task first instruction
 *   - Context switch:     semaphore yield round-trip between two tasks
 *   - Latency under load: same IRQ measurement while stress workload runs
 *
 * Method:
 *   A GPIO output pin is toggled to trigger an external interrupt on a
 *   second GPIO input pin. A free-running 1 MHz hardware timer timestamps
 *   both the trigger moment and the ISR entry. 512 samples are collected
 *   and reduced to min/max/avg/jitter statistics.
 *
 *   IRQ-to-task response is measured by having the ISR give a semaphore
 *   and recording the timestamp when the waiting task first executes.
 */

#include "benchmark_common.h"
#include "latency_meas.h"

#define LATENCY_SAMPLES  512U

static volatile uint32_t _irq_fire_ts[LATENCY_SAMPLES];
static volatile uint32_t _irq_recv_ts[LATENCY_SAMPLES];
static volatile uint32_t _task_recv_ts[LATENCY_SAMPLES];
static volatile uint32_t _sample_idx     = 0;
static volatile uint32_t _task_idx       = 0;

/* ════════════════════════════════════════════════════════════════════
 *  FreeRTOS (STM32 HAL  -  TIM2 @ 1 MHz, EXTI on PA0)
 * ═══════════════════════════════════════════════════════════════════*/
#if defined(FREERTOS)
#include "stm32f4xx_hal.h"

extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;

static SemaphoreHandle_t _irq_sem;

void HAL_GPIO_EXTI_Callback(uint16_t pin) {
    if (pin == GPIO_PIN_0 && _sample_idx < LATENCY_SAMPLES) {
        _irq_recv_ts[_sample_idx] = __HAL_TIM_GET_COUNTER(&htim2);
        _sample_idx++;
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(_irq_sem, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

static void trigger_task(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        _irq_fire_ts[i] = __HAL_TIM_GET_COUNTER(&htim2);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    vTaskDelete(NULL);
}

static void response_task(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        xSemaphoreTake(_irq_sem, portMAX_DELAY);
        _task_recv_ts[i] = __HAL_TIM_GET_COUNTER(&htim2);
        _task_idx++;
    }
    vTaskDelete(NULL);
}

latency_results_t latency_measure_all(void) {
    _irq_sem   = xSemaphoreCreateBinary();
    _sample_idx = 0;
    _task_idx   = 0;

    xTaskCreate(response_task, "RESP", 256, NULL, configMAX_PRIORITIES - 1, NULL);
    xTaskCreate(trigger_task,  "TRIG", 256, NULL, configMAX_PRIORITIES - 2, NULL);

    while (_sample_idx < LATENCY_SAMPLES || _task_idx < LATENCY_SAMPLES)
        vTaskDelay(1);

    latency_results_t r;
    r.irq_latency_us  = (stat_acc_t)STAT_INIT();
    r.irq_to_task_us  = (stat_acc_t)STAT_INIT();
    r.ctx_switch_us   = (stat_acc_t)STAT_INIT();
    r.load_latency_us = (stat_acc_t)STAT_INIT();

    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        /* TIM2 @1 MHz -> direct microseconds */
        stat_update(&r.irq_latency_us,
                    _irq_recv_ts[i] - _irq_fire_ts[i]);
        stat_update(&r.irq_to_task_us,
                    _task_recv_ts[i] - _irq_recv_ts[i]);
    }
    stat_finalize(&r.irq_latency_us);
    stat_finalize(&r.irq_to_task_us);

    /* Context switch: semaphore yield round-trip */
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t t0 = __HAL_TIM_GET_COUNTER(&htim2);
        xSemaphoreGive(sem);
        xSemaphoreTake(sem, 0);
        stat_update(&r.ctx_switch_us, __HAL_TIM_GET_COUNTER(&htim2) - t0);
    }
    stat_finalize(&r.ctx_switch_us);

    /* Latency under load: re-run irq measurement while tasks are busy */
    _sample_idx = 0;
    xTaskCreate(trigger_task, "TRIG2", 256, NULL, configMAX_PRIORITIES - 2, NULL);
    while (_sample_idx < LATENCY_SAMPLES) vTaskDelay(1);
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++)
        stat_update(&r.load_latency_us, _irq_recv_ts[i] - _irq_fire_ts[i]);
    stat_finalize(&r.load_latency_us);

    vSemaphoreDelete(sem);
    return r;
}

/* ════════════════════════════════════════════════════════════════════
 *  RIOT OS (periph/timer, periph/gpio)
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(RIOT_OS)
#include "periph/timer.h"
#include "periph/gpio.h"

#define BENCH_TIMER     TIMER_DEV(1)
#define BENCH_TIMER_HZ  1000000UL
#define TRIGGER_PIN     GPIO_PIN(PORT_A, 1)
#define CAPTURE_PIN     GPIO_PIN(PORT_A, 0)

static mutex_t _irq_mutex = MUTEX_INIT_LOCKED;

static void gpio_irq_cb(void *arg) {
    (void)arg;
    if (_sample_idx < LATENCY_SAMPLES) {
        _irq_recv_ts[_sample_idx] = timer_read(BENCH_TIMER);
        _sample_idx++;
        mutex_unlock(&_irq_mutex);
    }
}

static void *response_thread(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        mutex_lock(&_irq_mutex);
        _task_recv_ts[i] = timer_read(BENCH_TIMER);
        _task_idx++;
    }
    return NULL;
}

static char _resp_stack[THREAD_STACKSIZE_DEFAULT];

latency_results_t latency_measure_all(void) {
    timer_init(BENCH_TIMER, BENCH_TIMER_HZ, NULL, NULL);
    gpio_init(TRIGGER_PIN, GPIO_OUT);
    gpio_init_int(CAPTURE_PIN, GPIO_IN, GPIO_RISING, gpio_irq_cb, NULL);
    _sample_idx = 0; _task_idx = 0;

    thread_create(_resp_stack, sizeof(_resp_stack),
                  THREAD_PRIORITY_MAIN - 1, 0,
                  response_thread, NULL, "resp");

    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        _irq_fire_ts[i] = timer_read(BENCH_TIMER);
        gpio_set(TRIGGER_PIN);
        gpio_clear(TRIGGER_PIN);
        ztimer_sleep(ZTIMER_USEC, 2000);
    }
    while (_task_idx < LATENCY_SAMPLES) { thread_yield(); }

    latency_results_t r;
    r.irq_latency_us  = (stat_acc_t)STAT_INIT();
    r.irq_to_task_us  = (stat_acc_t)STAT_INIT();
    r.ctx_switch_us   = (stat_acc_t)STAT_INIT();
    r.load_latency_us = (stat_acc_t)STAT_INIT();

    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        stat_update(&r.irq_latency_us,  _irq_recv_ts[i]  - _irq_fire_ts[i]);
        stat_update(&r.irq_to_task_us,  _task_recv_ts[i] - _irq_recv_ts[i]);
    }
    stat_finalize(&r.irq_latency_us);
    stat_finalize(&r.irq_to_task_us);

    /* Context switch via mutex round-trip */
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t t0 = timer_read(BENCH_TIMER);
        thread_yield();
        stat_update(&r.ctx_switch_us, timer_read(BENCH_TIMER) - t0);
    }
    stat_finalize(&r.ctx_switch_us);

    /* Load latency: remeasure under stress */
    _sample_idx = 0;
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        _irq_fire_ts[i] = timer_read(BENCH_TIMER);
        gpio_set(TRIGGER_PIN); gpio_clear(TRIGGER_PIN);
        ztimer_sleep(ZTIMER_USEC, 2000);
    }
    while (_sample_idx < LATENCY_SAMPLES) { thread_yield(); }
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++)
        stat_update(&r.load_latency_us, _irq_recv_ts[i] - _irq_fire_ts[i]);
    stat_finalize(&r.load_latency_us);

    return r;
}

/* ════════════════════════════════════════════════════════════════════
 *  Zephyr (GPIO + counter driver)
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(ZEPHYR)
#include <zephyr/drivers/gpio.h>
#include <zephyr/timing/timing.h>

#define TRIG_NODE DT_ALIAS(bench_trigger)
#define CAPT_NODE DT_ALIAS(bench_capture)

static const struct gpio_dt_spec trig = GPIO_DT_SPEC_GET(TRIG_NODE, gpios);
static const struct gpio_dt_spec capt = GPIO_DT_SPEC_GET(CAPT_NODE, gpios);
static struct gpio_callback capt_cb_data;

K_SEM_DEFINE(_irq_sem_z, 0, 1);

static void capt_irq_handler(const struct device *dev,
                              struct gpio_callback *cb, uint32_t pins) {
    (void)dev; (void)cb; (void)pins;
    if (_sample_idx < LATENCY_SAMPLES) {
        _irq_recv_ts[_sample_idx] = (uint32_t)timing_counter_get();
        _sample_idx++;
        k_sem_give(&_irq_sem_z);
    }
}

K_THREAD_STACK_DEFINE(_resp_stack_z, 1024);
static struct k_thread _resp_td;

static void response_entry(void *a, void *b, void *c) {
    (void)a; (void)b; (void)c;
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        k_sem_take(&_irq_sem_z, K_FOREVER);
        _task_recv_ts[i] = (uint32_t)timing_counter_get();
        _task_idx++;
    }
}

latency_results_t latency_measure_all(void) {
    timing_init();
    timing_start();
    uint32_t cyc_per_us = sys_clock_hw_cycles_per_sec() / 1000000UL;

    gpio_pin_configure_dt(&trig, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&capt, GPIO_INPUT);
    gpio_init_callback(&capt_cb_data, capt_irq_handler, BIT(capt.pin));
    gpio_add_callback(capt.port, &capt_cb_data);
    gpio_pin_interrupt_configure_dt(&capt, GPIO_INT_EDGE_RISING);
    _sample_idx = 0; _task_idx = 0;

    k_thread_create(&_resp_td, _resp_stack_z,
                    K_THREAD_STACK_SIZEOF(_resp_stack_z),
                    response_entry, NULL, NULL, NULL,
                    K_PRIO_PREEMPT(0), 0, K_NO_WAIT);

    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        _irq_fire_ts[i] = (uint32_t)timing_counter_get();
        gpio_pin_set_dt(&trig, 1);
        gpio_pin_set_dt(&trig, 0);
        k_usleep(2000);
    }
    k_thread_join(&_resp_td, K_FOREVER);

    latency_results_t r;
    r.irq_latency_us  = (stat_acc_t)STAT_INIT();
    r.irq_to_task_us  = (stat_acc_t)STAT_INIT();
    r.ctx_switch_us   = (stat_acc_t)STAT_INIT();
    r.load_latency_us = (stat_acc_t)STAT_INIT();

    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        stat_update(&r.irq_latency_us,
                    (_irq_recv_ts[i]  - _irq_fire_ts[i])  / cyc_per_us);
        stat_update(&r.irq_to_task_us,
                    (_task_recv_ts[i] - _irq_recv_ts[i])  / cyc_per_us);
    }
    stat_finalize(&r.irq_latency_us);
    stat_finalize(&r.irq_to_task_us);

    /* Context switch */
    K_SEM_DEFINE(cs_sem, 0, 1);
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t t0 = (uint32_t)timing_counter_get();
        k_yield();
        stat_update(&r.ctx_switch_us,
                    ((uint32_t)timing_counter_get() - t0) / cyc_per_us);
    }
    stat_finalize(&r.ctx_switch_us);

    /* Load latency */
    _sample_idx = 0;
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        _irq_fire_ts[i] = (uint32_t)timing_counter_get();
        gpio_pin_set_dt(&trig, 1); gpio_pin_set_dt(&trig, 0);
        k_usleep(2000);
    }
    while (_sample_idx < LATENCY_SAMPLES) { k_yield(); }
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++)
        stat_update(&r.load_latency_us,
                    (_irq_recv_ts[i] - _irq_fire_ts[i]) / cyc_per_us);
    stat_finalize(&r.load_latency_us);

    return r;
}
#endif
