//Included header files
#include "benchmark_common.h"
#include "latency_meas.h"

//Number of latency samples to calculate latency_stats_t values
#define LATENCY_SAMPLES  100U

//Static volatile variables that stores the time right before
// an interrupt should be triggered, when the interrupt is
// actually fired, when the task the interrupt was fired to
// wake up gets control of the CPU, the index of when the
// interrupt was actually fire, and the index of the response
// time of the responding task
static volatile uint32_t _irq_fire_ts[LATENCY_SAMPLES];
static volatile uint32_t _irq_recv_ts[LATENCY_SAMPLES];
static volatile uint32_t _task_recv_ts[LATENCY_SAMPLES];
static volatile uint32_t _sample_idx     = 0;
static volatile uint32_t _task_idx       = 0;

#if defined(FREERTOS_PICO)
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include <stdio.h> 

// Define GP14 as the pin to send the interrupt and
// GP15 as the pin to receive the interrupt
#define TRIG_PIN 14
#define CAPT_PIN 15
//Initialize a static semaphore
static SemaphoreHandle_t _irq_sem;

//Runs everytime the physical interrupt occurs
static void gpio_callback(uint gpio, uint32_t events) {
    //Only run the code within this function if the interrupt was
    // received on the defined capture pin and there's still
    //space in the _irq_recv_ts array, check capture pin first
    // set set interrupt flag back to 0 and then check the number
    // of samples
    if (gpio == CAPT_PIN && _sample_idx < LATENCY_SAMPLES) {
        // Record the exact microsecond the hardware saw the signal
        _irq_recv_ts[_sample_idx] = (uint32_t)time_us_64();
        _sample_idx++;
        //Set a boolean to false (used BaseType_t as it is a portable
        // datatype definition used for performance-critical variables)
        // and pass it to xSemaphoreGiveFromISR to have the immediate
        // assumption be that the task waking up thanks to the interrupt
        // is not higher priority than the task that was interrupted
        BaseType_t woken = pdFALSE;
        //Firing the interrupt stopped the trigger task and woke up the
        // response task, calling xSemaphoreGiveFromISR with _irq_sem
        // to set the semaphore to 1 and set woken to true if the task
        // that was woken up was higher priority than the task that was
        // interrupted (in this case it will be if the test includes 
        // response task as it has a higher priority than trigger task,
        // otherwise it won't)
        xSemaphoreGiveFromISR(_irq_sem, &woken);
        //If woken is false do nothing, otherwise execute a context
        // switch from the interrupted task to the woken up task
        portYIELD_FROM_ISR(woken);
    }
}

//Fires an interrupt by sending voltage from GP21 to GP22
static void trigger_task(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        //Get the current time and store it in the array
        // that stores times right before an interrupt is fired
        _irq_fire_ts[i] = (uint32_t)time_us_64();
        //Set GP21 to high to send voltage to GP22 (to GP22 due to physical
        // wiring)
        gpio_put(TRIG_PIN, 1);
        //Immediately reset GP21 to low to stop sending voltage to GP21 (we can
        // immediately stop since whatever voltage was sent in an instant is enough
        // to trigger an interrupt
        gpio_put(TRIG_PIN, 0);
        //Sleep for 2 ms
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    //Delete task after execution
    vTaskDelete(NULL);
}

//Runs immediately after gpio_callback if this task is active
static void response_task(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        //Stop execution of this task until the semaphore is taken or it times out
        // after 100 milliseconds
        if (xSemaphoreTake(_irq_sem, pdMS_TO_TICKS(100)) == pdFALSE) {
            break;
        }
        //Get the current time and put it in the array that stores the response time
        // of the response task after the interrupt
        _task_recv_ts[i] = (uint32_t)time_us_64();
        //Iterate the response task index
        _task_idx++;
    }
    //Delete task after execution
    vTaskDelete(NULL);
}

latency_results_t latency_measure_all(void) {
    //Create the semaphore and set the index variables to 0
    _irq_sem = xSemaphoreCreateBinary();
    _sample_idx = 0;
    _task_idx = 0;

    //Initialize the trigger pin
    gpio_init(TRIG_PIN);
    //Set the pin's electrical direction to output
    gpio_set_dir(TRIG_PIN, GPIO_OUT);
    //Set the trigger pin to low to prevent accidental interrupts
    gpio_put(TRIG_PIN, 0);

    //Intialize the capture pin
    gpio_init(CAPT_PIN);
    //Set the capture pin's electrical direction to input
    gpio_set_dir(CAPT_PIN, GPIO_IN);

    //Attach an interrupt to the capture pin, set the interrupt to trigger when the capture
    // pin is set to high, enable the interrupt to trigger, and jump to gpio_callback when
    // the interrupt occurs
    gpio_set_irq_enabled_with_callback(CAPT_PIN, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);

    //Create a trigger task and a response task, ensure the response task as a higher
    // priority than the trigger task
    xTaskCreate(response_task, "RESP", 256, NULL, configMAX_PRIORITIES - 1, NULL);
    xTaskCreate(trigger_task,  "TRIG", 256, NULL, configMAX_PRIORITIES - 2, NULL);

    //Stop looping once both sample arrays are full, poll if they are full every
    // 2 milliseconds
    uint32_t timeout = 0;
    while ((_sample_idx < LATENCY_SAMPLES || _task_idx < LATENCY_SAMPLES) && timeout < 2000) {
        vTaskDelay(1);
        timeout++;
    }
    if (timeout >= 2000) {
        printf("Time out occured during Latency Test\n");
    }

    //Initialize struct and all values in the struct (to 0 except min which is set
    // to the maximum unsigned 32-bit integer)
    latency_results_t r;
    r.irq_latency_us  = (stat_acc_t)STAT_INIT();
    r.irq_to_task_us  = (stat_acc_t)STAT_INIT();
    r.ctx_switch_us   = (stat_acc_t)STAT_INIT();
    r.load_latency_us = (stat_acc_t)STAT_INIT();

    //Use stat update static inline function to get sum, samples, min, and max
    // values by iterating through the difference of the recv and fire lists
    // and the task and recv lists
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        stat_update(&r.irq_latency_us, _irq_recv_ts[i] - _irq_fire_ts[i]);
        stat_update(&r.irq_to_task_us, _task_recv_ts[i] - _irq_recv_ts[i]);
    }
    
    //Use stat finalize static inline function to get average, and jitter
    stat_finalize(&r.irq_latency_us);
    stat_finalize(&r.irq_to_task_us);

    //Create a semaphore
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    for (uint32_t i = 0; i < 100; i++) {
        //Get the current time
        uint32_t t0 = (uint32_t)time_us_64();
        //Immediately give and take the semaphore to get the time it takes for
        // the CPU to switch from this task to execute internal scheduling logic
        // in FreeRTOS' Kernel's protected memory space and then back again
        xSemaphoreGive(sem);
        //Pass 0 to xSemaphoreTake to make the function non-blocking so that
        // the time is immediately taken once this task begins executing again
        // without have to wait for the semaphore to beome available first 
        xSemaphoreTake(sem, 0);
        //Use static update on context switching data with the current time and
        // the time before giving up the semaphore
        stat_update(&r.ctx_switch_us, (uint32_t)time_us_64() - t0);
    }

    //Use stat finalize on the context switching data
    stat_finalize(&r.ctx_switch_us);

    //Reset the sample index to 0
    _sample_idx = 0;
    //Create another trigger task
    xTaskCreate(trigger_task, "TRIG2", 256, NULL, configMAX_PRIORITIES - 2, NULL);
    //Poll every 2 milliseconds to see if the samples list has been refilled and
    // then move on once it has
    timeout = 0;
    while (_sample_idx < LATENCY_SAMPLES && timeout < 2000) {
        vTaskDelay(1);
        timeout++;
    }
    if (timeout >= 2000) {
        printf("Time out occured during Latency Test\n");
    }


    //Update and finalize another set of interrupt response times (like with
    // irq_latency_us) without the task response times (_task_recv_ts)
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++)
        stat_update(&r.load_latency_us, _irq_recv_ts[i] - _irq_fire_ts[i]);
    stat_finalize(&r.load_latency_us);

    // Disable the interrupt before leaving so it doesn't fire randomly later
    gpio_set_irq_enabled(CAPT_PIN, GPIO_IRQ_EDGE_RISE, false);
    
    //Delete the semaphores and return the metrics struct
    vSemaphoreDelete(sem);
    vSemaphoreDelete(_irq_sem);
    return r;
}

#elif defined(RIOT_OS)
#include "periph/timer.h"
#include "periph/gpio.h"

//Get timer from 0 indexed list of timers (i.e.
// hardware timer 2)
#define BENCH_TIMER     TIMER_DEV(1)
//Set the timer to 1000000 ticks per second
#define BENCH_TIMER_HZ  1000000UL
//Set the trigger pin to 1 and the capture pin to 0
#define TRIGGER_PIN     GPIO_PIN(PORT_A, 1)
#define CAPTURE_PIN     GPIO_PIN(PORT_A, 0)

//Use RIOT specific mutex behavior to initialize
// a static mutex and then immediately lock it
static mutex_t _irq_mutex = MUTEX_INIT_LOCKED;

//When an interrupt is triggered, check if there are more samples
// that need to be recorded and if there are record the time in
// the interrupt recv list, iterate the list index, and use RIOT
// specific behavior to unlock the mutex (either locked during 
// initialization or in the response_thread)
static void gpio_irq_cb(void *arg) {
    (void)arg;
    if (_sample_idx < LATENCY_SAMPLES) {
        _irq_recv_ts[_sample_idx] = timer_read(BENCH_TIMER);
        _sample_idx++;
        mutex_unlock(&_irq_mutex);
    }
}

//Wait until the mutex can be locked and then lock it,
// record the time in the task recv list, and iterate
// the list index
static void *response_thread(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        mutex_lock(&_irq_mutex);
        _task_recv_ts[i] = timer_read(BENCH_TIMER);
        _task_idx++;
    }
    return NULL;
}

//Allocate memory for response thread
static char _resp_stack[THREAD_STACKSIZE_DEFAULT];

latency_results_t latency_measure_all(void) {
    //Initialize timer, trigger pin, capture pin, and list indices
    timer_init(BENCH_TIMER, BENCH_TIMER_HZ, NULL, NULL);
    gpio_init(TRIGGER_PIN, GPIO_OUT);
    gpio_init_int(CAPTURE_PIN, GPIO_IN, GPIO_RISING, gpio_irq_cb, NULL);
    _sample_idx = 0; _task_idx = 0;

    //Create response thread with higher priority than the current thread
    // (which is used to trigger interrupts)
    thread_create(_resp_stack, sizeof(_resp_stack),
                  THREAD_PRIORITY_MAIN - 1, 0,
                  response_thread, NULL, "resp");

    //Record the current time immediately before firing an interrupt,
    // fire the interrupt and reset the pin, and sleep for 2 milliseconds
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        _irq_fire_ts[i] = timer_read(BENCH_TIMER);
        gpio_set(TRIGGER_PIN);
        gpio_clear(TRIGGER_PIN);
        ztimer_sleep(ZTIMER_USEC, 2000);
    }
    //Prevents this thread from continuing before the response thread
    // has finished its work
    while (_task_idx < LATENCY_SAMPLES) { thread_yield(); }

    //Initialize, update, and finalize stats related to the above experiment with
    // functions found in benchmark_common.h
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

    //Take the current time, yield the thread to trigger a kernel mode trap
    // and then take the time again once this thread begins executing again
    // to record the context switching time, get accumulated values, min, 
    // and max during the loop and get calculated values after the loop
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t t0 = timer_read(BENCH_TIMER);
        thread_yield();
        stat_update(&r.ctx_switch_us, timer_read(BENCH_TIMER) - t0);
    }
    stat_finalize(&r.ctx_switch_us);

    //Reset the sample index and get irq_latency_us values when there is no
    // response task and store it in load_latency_us, then return the metrics
    // struct
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

#elif defined(ZEPHYR)
#include <zephyr/drivers/gpio.h>
#include <zephyr/timing/timing.h>

//Get alias of trigger and capture pins from Device Tree Source file
#define TRIG_NODE DT_ALIAS(bench_trigger)
#define CAPT_NODE DT_ALIAS(bench_capture)

//Use aliases to get information about the pins from the Device Tree
// Source file, have the pins read as gpios
static const struct gpio_dt_spec trig = GPIO_DT_SPEC_GET(TRIG_NODE, gpios);
static const struct gpio_dt_spec capt = GPIO_DT_SPEC_GET(CAPT_NODE, gpios);
//Initialize a gpio callback struct
static struct gpio_callback capt_cb_data;
//Initialze a binary semaphore, set the initial count to 0 and the maximum
// count to 1
K_SEM_DEFINE(_irq_sem_z, 0, 1);

//When an interrupt is called and if the interrupt recv list isn't full,
// enter current time into interrupt recv list, interate interrupt recv
// index, and set the semaphore to 1
//Note that dev, cb, and pins are requires function parameters for zephyr
// interrupt service routines eventhough they are unused here
static void capt_irq_handler(const struct device *dev,
                              struct gpio_callback *cb, uint32_t pins) {
    (void)dev; (void)cb; (void)pins;
    if (_sample_idx < LATENCY_SAMPLES) {
        _irq_recv_ts[_sample_idx] = (uint32_t)timing_counter_get();
        _sample_idx++;
        k_sem_give(&_irq_sem_z);
    }
}

//Allocate memory for the response task and initialize its Thread
// control block
K_THREAD_STACK_DEFINE(_resp_stack_z, 1024);
static struct k_thread _resp_td;

//Wait until the semaphore is 1 so that this task can set it back
// to 0, then record the current time into task recv and interate
// its list index
static void response_entry(void *a, void *b, void *c) {
    (void)a; (void)b; (void)c;
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        k_sem_take(&_irq_sem_z, K_FOREVER);
        _task_recv_ts[i] = (uint32_t)timing_counter_get();
        _task_idx++;
    }
}

latency_results_t latency_measure_all(void) {
    //Initialize and start timer
    timing_init();
    timing_start();
    //Get the number of hardware cycles per microsecond
    uint32_t cyc_per_us = sys_clock_hw_cycles_per_sec() / 1000000UL;

    //Configure the trigger pin as an inactive output pin (made active
    // when set to 1) and the capture pin as an actively listening input
    // pin
    gpio_pin_configure_dt(&trig, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&capt, GPIO_INPUT);
    //Fill out capt_cb_data information, link capt.pin to capt_irq_handler
    gpio_init_callback(&capt_cb_data, capt_irq_handler, BIT(capt.pin));
    //Link capt_cb_data to the correct hardware GPIO port controller
    gpio_add_callback(capt.port, &capt_cb_data);
    //Set the interrupt to trigger when the voltage goes from 0 to 3.3
    gpio_pin_interrupt_configure_dt(&capt, GPIO_INT_EDGE_RISING);
    //Initialize list indices
    _sample_idx = 0; _task_idx = 0;

    //Create response thread, give it the highest possible preemptive
    // priority and make it launch immediately (K_NO_WAIT)
    k_thread_create(&_resp_td, _resp_stack_z,
                    K_THREAD_STACK_SIZEOF(_resp_stack_z),
                    response_entry, NULL, NULL, NULL,
                    K_PRIO_PREEMPT(0), 0, K_NO_WAIT);

    //Get time before interrupt into interrupt fire, then
    // set pin to high and immediately back to low to trigger
    // an interrupt, then sleep for 2 milliseconds
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        _irq_fire_ts[i] = (uint32_t)timing_counter_get();
        gpio_pin_set_dt(&trig, 1);
        gpio_pin_set_dt(&trig, 0);
        k_usleep(2000);
    }
    //Wait forever until _resp_td finishes executing and
    // is destroyed
    k_thread_join(&_resp_td, K_FOREVER);

    //Run benchmark_common.h functions to initialize
    // latency result values and finialze values related
    // to the above experiment
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

    K_SEM_DEFINE(cs_sem, 0, 1);
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t t0 = (uint32_t)timing_counter_get();
        k_yield();
        stat_update(&r.ctx_switch_us,
                    ((uint32_t)timing_counter_get() - t0) / cyc_per_us);
    }
    stat_finalize(&r.ctx_switch_us);

    //Get irq_latency_us values without any response task
    _sample_idx = 0;
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++) {
        _irq_fire_ts[i] = (uint32_t)timing_counter_get();
        gpio_pin_set_dt(&trig, 1); gpio_pin_set_dt(&trig, 0);
        k_usleep(2000);
    }
    //Ensure the final interrupt service routine finishes executing
    // before calculating and finialzing results
    while (_sample_idx < LATENCY_SAMPLES) { k_yield(); }
    for (uint32_t i = 0; i < LATENCY_SAMPLES; i++)
        stat_update(&r.load_latency_us,
                    (_irq_recv_ts[i] - _irq_fire_ts[i]) / cyc_per_us);
    stat_finalize(&r.load_latency_us);
    return r;
}
#endif
