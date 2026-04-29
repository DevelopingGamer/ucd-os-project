//Included header files
#include "benchmark_common.h"
#include "workload_sim.h"

//Static volative integers storing the number of sensor
// deadlines, the number of sensor deadlines missed, the
// number of 64 byte network messages successfully sent,
// the number of 64 byte network messages lost, the number
// of sensor deadline misses again (redundent), the number
// of sensor deadlines (redundent), the number of successful
// interrupts during the interrupt stress test, the
// number of failed interrupts during the interrupt stress
// test, and the number of priority inversion locks 
static volatile uint32_t _sensor_samples   = 0;
static volatile uint32_t _sensor_missed    = 0;
static volatile uint32_t _net_sent         = 0;
static volatile uint32_t _net_lost         = 0;
static volatile uint32_t _deadline_misses  = 0;
static volatile uint32_t _deadline_total   = 0;
static volatile uint32_t _hf_irq_count     = 0;
static volatile uint32_t _hf_irq_miss      = 0;
static volatile uint32_t _priority_inv_cnt = 0;

//Only compile if code was compiled with -DFREERTOS_PICO compiler 
// flag
#if defined(FREERTOS_PICO)
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/timer.h"

extern bool dht22_read(uint pin, float *temp, float *humidity);
extern bool wifi_send_udp_packet(uint8_t *data, size_t len);

#define DHT22_PIN 20

static void sensor_task(void *arg) {
    //Suppress unused variable warning (FreeRTOS requires accepting a
    // void pointer as an argument eventhough its unused)
    (void)arg;
    //Get the current timestamp, calcuate the expected timestamp of
    // the first deadline, and convert the deadline slack from
    // microseconds to hardware ticks 
    TickType_t last_wake  = xTaskGetTickCount();
    TickType_t expected   = last_wake + pdMS_TO_TICKS(SENSOR_PERIOD_MS);
    TickType_t DEADLINE_SLACK_TICKS = pdMS_TO_TICKS((DEADLINE_SLACK_US / 1000U));
    //Iterate the number of samples to be attempted to be collected
    for (uint32_t i = 0; i < SENSOR_TOTAL_SAMPLES; i++) {
        //Sleep for 10ms from last_wake and update last_wake after waking up
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
        //Iterate the number of deadlines
        _deadline_total++;
        //If the current time is more than the expected deadline plus the slack, iterate
        // deadline misses and sensor misses
        if (TICK_TO_MS(xTaskGetTickCount()) > expected + DEADLINE_SLACK_TICKS) { 
            _deadline_misses++; 
            _sensor_missed++; 
        }
        //Update expected deadline
        expected += pdMS_TO_TICKS(SENSOR_PERIOD_MS);
        //Read data from the DHT22
        float temp, hum;
        if (dht22_read(DHT22_PIN, &temp, &hum)) {
            //Iterate the number of sensor samples
            _sensor_samples++;
        }
    }
    //Delete sensor task after completing execution
    vTaskDelete(NULL);
}

//Initialize an array to store the message to be sent through networking
static uint8_t _net_msg[NET_MSG_SIZE];
//Simulates a network task
static void net_task(void *arg) {
    //Supresses unused variable warning
    (void)arg;
    //Generate integers for message (ex. 0, 1, 2, 3,..., 64)
    for (uint32_t i = 0; i < NET_MSG_SIZE; i++) _net_msg[i] = (uint8_t)i;
    //Send the message NET_MSG_COUNT times
    for (uint32_t i = 0; i < NET_MSG_COUNT; i++) {
        //Use the Raspberry Pi Pico 2W's network stack and CYW43439 WiFi chip 
        // to send the message over UDP, if the packet was successfully pushed
        // to the CYW43439's transmission queue iterate _net_sent, otherwise
        // iterate _net_lost
        if (wifi_send_udp_packet(_net_msg, NET_MSG_SIZE)) {
            _net_sent++;
        } else {
            _net_lost++;
        }
        //Sleep for 5 ms to simulate realistic burst traffic and give
        // other tasks time to run
        vTaskDelay(pdMS_TO_TICKS(5U));
    }
    //Delete network task after completing execution
    vTaskDelete(NULL);
}

//When the timer hits 0 an interrupt is triggered to increment
// the successful interrupt counter, return true so that the
// interrupt timer is reset and begins counting down to the 
// next interrupt instead of deleting the timer
bool PICO_REPEATING_Callback(struct repeating_timer *t) {
    _hf_irq_count++;
    return true;
}

//Calculate the execpted number of interrupts, set the successful interrupt
// counter to 0, initialize a repeating timer struct, set the delay between
// each interrupt so that it triggers HF_IRQ_RATE_HZ times per second and
// mutiply the delay -1 to trigger the interrupt every 1 second instead of
// waiting 1 second after the callback function finishes each time, attach
// the delay and the callback function to the repearting timer struct,
// sleep for HF_IRQ_DURATION_MS to allow the test to run for 
// HF_IRQ_DURATION_MS and then calcualte the number of interrupts that
// weren't able to trigger if the microcontroller was not able to
// sucessfully trigger all of the interrupts
static void hf_irq_stress_run(void) {
    uint32_t expected = (HF_IRQ_RATE_HZ * HF_IRQ_DURATION_MS) / 1000U;
    _hf_irq_count = 0;
    struct repeating_timer timer;
    int32_t delay_us = -(1000000 / HF_IRQ_RATE_HZ);
    add_repeating_timer_us(delay_us, PICO_REPEATING_Callback, NULL, &timer);
    vTaskDelay(pdMS_TO_TICKS(HF_IRQ_DURATION_MS));
    cancel_repeating_timer(&timer);
    //Set the interrupt miss counter to 0
    _hf_irq_miss = 0;
    if (_hf_irq_count < expected)
        _hf_irq_miss = expected - _hf_irq_count;
}

//Simulate background workload for concurrent tasks
static void dummy_task(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < SENSOR_TOTAL_SAMPLES; i++) {
        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
    vTaskDelete(NULL);
}

workload_results_t workload_run(void) {
    //Initialize variables various metrics to
    _sensor_samples = _sensor_missed = _net_sent = _net_lost = 0;
    _deadline_misses = _deadline_total = _hf_irq_count = 0;

    //Crate the sensor and net tasks
    xTaskCreate(sensor_task, "SENS", 256, NULL, tskIDLE_PRIORITY + 3, NULL);
    xTaskCreate(net_task,    "NET",  512, NULL, tskIDLE_PRIORITY + 2, NULL);

    //Create various background dummy tasks of differing priorities
    for (uint32_t i = 0; i < CONCURRENT_TASKS; i++) {
        UBaseType_t pri = tskIDLE_PRIORITY + 1 + (i % 4);
        xTaskCreate(dummy_task, "CT", 128, NULL, pri, NULL);
    }

    //Sleep for the duration of the sensor task with a 500ms buffer, then run
    // the interrupt stress test
    vTaskDelay(pdMS_TO_TICKS(SENSOR_TOTAL_SAMPLES * SENSOR_PERIOD_MS + 500U));
    hf_irq_stress_run();

    //Initialize the workload results struct
    workload_results_t r;
    //Set the struct values to the global values and then return the struct
    r.sensor_samples_total  = _sensor_samples;
    r.sensor_missed_samples = _sensor_missed;
    r.sensor_miss_rate_pct  = (_deadline_total > 0)
        ? (float)_sensor_missed * 100.0f / (float)_deadline_total : 0.0f;
    r.net_msgs_sent          = _net_sent;
    r.net_msgs_lost          = _net_lost;
    r.net_loss_pct           = ((_net_sent + _net_lost) > 0)
        ? (float)_net_lost * 100.0f / (float)(_net_sent + _net_lost) : 0.0f;
    r.concurrent_tasks_run   = CONCURRENT_TASKS;
    r.hf_irq_count           = _hf_irq_count;
    r.hf_irq_miss_count      = _hf_irq_miss;
    return r;
}

//Only compile if code was compiled with -DRIOT_OS compiler 
// flag
#elif defined(RIOT_OS)
//Include header file for DHT device driver interface
#include "dht.h"
//Include header file for dht pin mappings
#include "dht_params.h"
//Include headers file for UDP communication
#include "net/sock/udp.h"
#include "net/gnrc.h"

//Allocate memory for sensor, network, and background tasks
static char _sens_stack[THREAD_STACKSIZE_DEFAULT];
static char _net_stack[THREAD_STACKSIZE_DEFAULT];
static char _ct_stacks[CONCURRENT_TASKS][THREAD_STACKSIZE_MINIMUM];

//Thread to execute sensor task
static void *sensor_thread(void *arg) {
    (void)arg;
    //Get the expected time of the first deadline completion
    uint32_t expected = ztimer_now(ZTIMER_MSEC) + SENSOR_PERIOD_MS;
    
    //Initialize the DHT22 with the data pin defined in dht_params.h
    dht_t dev;
    dht_init(&dev, &dht_params[0]);

    //Iterate for the total number of deadlines to attempt
    for (uint32_t i = 0; i < SENSOR_TOTAL_SAMPLES; i++) {
        //Increment the number of deadline attempts
        _deadline_total++;
        //If the current time is past the expected time plus the slack
        // consider the deadline missed and increment the deadline and
        // sensor miss counters
        if (ztimer_now(ZTIMER_MSEC) > expected + (DEADLINE_SLACK_US / 1000U))
            { _deadline_misses++; _sensor_missed++; }
        //Update expected deadline
        expected += SENSOR_PERIOD_MS;
        
        //If the temperature and humidity can be successfully read from the
        // DHT22 iterate the number of sensor samples successfully collected
        int16_t temp, hum;
        if (dht_read(&dev, &temp, &hum) == 0) {
            _sensor_samples++;
        }
        //Sleep for SENSOR_PERIOD_MS milliseconds
        ztimer_sleep(ZTIMER_MSEC, SENSOR_PERIOD_MS);
    }
    //Kill thread after execution
    return NULL;
}

//Thread to execute network task
//Uses IPv6 since no IPv4 implementation exists for the RP2040 that
// will allow the code to compile, note that the IPv6 implementation
// still cannot communicate with the Infineon CYW 43439 wireless chip
// to allow messages to actually be sent over UDP
static void *net_thread(void *arg) {
    (void)arg;
    //Initialize message with NET_MSG_SIZE bytes
    uint8_t msg[NET_MSG_SIZE];
    //Fill the msg array with numbers
    for (uint32_t i = 0; i < NET_MSG_SIZE; i++) msg[i] = (uint8_t)i;

    //Send UDP packets to an IPv6 Address, port 8080
    sock_udp_ep_t remote = { .family = AF_INET6, .port = 8080 };
    //Translate the human readable link-local IPv6 address fe80::1 to machine
    // readable 32-bit binary format and store the converted data to
    //the memory address of remote.addr.ipv6
    ipv6_addr_from_str((ipv6_addr_t *)&remote.addr.ipv6, "fe80::1");

    //Attempt to send message NET_MSG_COUNT times 
    for (uint32_t i = 0; i < NET_MSG_COUNT; i++) {
        //Send the message over any socket available to the destination address
        // defined by remote then close the socket
        ssize_t res = sock_udp_send(NULL, msg, NET_MSG_SIZE, &remote);
        //If the packet was send successfully over UDP iterate _net_sent, otherwise
        // iterate _net_lost
        if (res >= 0) _net_sent++;
        else _net_lost++;
        //Sleep for 5 milliseconds
        ztimer_sleep(ZTIMER_MSEC, 5U);
    }
    //Kill thread after execution
    return NULL;
}

/*Original IPv4 version of the code
static void *net_thread(void *arg) {
    (void)arg;
    //Initialize message with NET_MSG_SIZE bytes
    uint8_t msg[NET_MSG_SIZE];
    //Fill the msg array with numbers
    for (uint32_t i = 0; i < NET_MSG_SIZE; i++) msg[i] = (uint8_t)i;

    //Send UDP packets to an IPv4 Address, port 8080
    sock_udp_ep_t remote = { .family = AF_INET, .port = 8080 };
    //Translate the human readable private IPv4 address 192.168.1.100 to machine
    // readable 32-bit binary format and store the converted data to
    //the memory address of remote.addr.ipv4
    ipv4_addr_from_str((ipv4_addr_t *)&remote.addr.ipv4, "192.168.1.100");

    //Attempt to send message NET_MSG_COUNT times 
    for (uint32_t i = 0; i < NET_MSG_COUNT; i++) {
        //Send the message over any socket available to the destination address
        // defined by remote then close the socket
        ssize_t res = sock_udp_send(NULL, msg, NET_MSG_SIZE, &remote);
        //If the packet was send successfully over UDP iterate _net_sent, otherwise
        // iterate _net_lost
        if (res >= 0) _net_sent++;
        else _net_lost++;
        //Sleep for 5 milliseconds
        ztimer_sleep(ZTIMER_MSEC, 5U);
    }
    //Kill thread after execution
    return NULL;
}
*/

//Every time the hardware timer hits 0 this function is ran to increment the
// sucessful interrupt counter
static void hf_irq_cb(void *arg, int ch) { (void)arg; (void)ch; _hf_irq_count++; }

static void hf_irq_stress_run(void) {
    //Get the expected number of interrupts during the duration of the test
    uint32_t expected    = (HF_IRQ_RATE_HZ * HF_IRQ_DURATION_MS) / 1000U;
    //Get the amount of time between interrupts in microseconds
    uint32_t interval_us = 1000000U / HF_IRQ_RATE_HZ;
    //Set the interrupt counter to 0
    _hf_irq_count = 0;
    //Use hardware timer 2, set the base speed of the timer to 1MHZ, and attach
    // hf_irq_cb to the timer
    timer_init(TIMER_DEV(2), 1000000U, hf_irq_cb, NULL);
    //Set hardware timer 2 to count up to interval_us microseconds and then
    // call an interrupt and reset to 0
    timer_set_periodic(TIMER_DEV(2), 0, interval_us, TIM_FLAG_RESET_ON_MATCH);
    //Sleep for the duration of the test
    ztimer_sleep(ZTIMER_MSEC, HF_IRQ_DURATION_MS);
    //Stop hardware timer 2 after the test
    timer_stop(TIMER_DEV(2));
    //Set the interrupt miss counter to 0
    _hf_irq_miss = 0;
    //Calculate the number of interrupt misses if there were any
    if (_hf_irq_count < expected) _hf_irq_miss = expected - _hf_irq_count;
}

//Dummy thread for concurrent threads
static void *dummy_thread(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < SENSOR_TOTAL_SAMPLES; i++) {
        ztimer_sleep(ZTIMER_MSEC, SENSOR_PERIOD_MS);
    }
    return NULL;
}

workload_results_t workload_run(void) {
    //Initialize various metrics to 0
    _sensor_samples = _sensor_missed = _net_sent = _net_lost = 0;
    _deadline_misses = _deadline_total = _hf_irq_count = 0;

    //Create sensor, network, and background threads
    thread_create(_sens_stack, sizeof(_sens_stack),
                  THREAD_PRIORITY_MAIN - 3, 0, sensor_thread, NULL, "sens");
    thread_create(_net_stack,  sizeof(_net_stack),
                  THREAD_PRIORITY_MAIN - 2, 0, net_thread,   NULL, "net");
    for (uint32_t i = 0; i < CONCURRENT_TASKS; i++) {
        int pri = THREAD_PRIORITY_MAIN - 1 - (int)(i % 4);
        thread_create(_ct_stacks[i], THREAD_STACKSIZE_MINIMUM,
                      pri, 0, dummy_thread, NULL, "ct");
    }

    //Sleep for the duration of the sensor task with 500ms of extra time
    // and then run the interrupt stress test
    ztimer_sleep(ZTIMER_MSEC, SENSOR_TOTAL_SAMPLES * SENSOR_PERIOD_MS + 500U);
    hf_irq_stress_run();

    //Initialize the struct, set struct values to global values, and return
    // the struct
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

//Compile only if compiled with -DZEPHYR compiler flag
#elif defined(ZEPHYR)
//Include zephyr sensor and socket drivers
#include <zephyr/drivers/sensor.h>
#include <zephyr/net/socket.h>

//Allocate memory for sensor, network, and background threads
K_THREAD_STACK_DEFINE(_sens_stack_z, 1024);
K_THREAD_STACK_DEFINE(_net_stack_z,  1024);
K_THREAD_STACK_ARRAY_DEFINE(_ct_stacks_z, CONCURRENT_TASKS, 512);
//Create thread control blocks (one for the sensor and network task and
// CONCURRENT_TASKS for the background tasks)
static struct k_thread _sens_td, _net_td, _ct_td[CONCURRENT_TASKS];

//Sensor thread
//Sensor thread
static void sensor_entry(void *a, void *b, void *c) {
    //Have 3 void pointers as function parameters due to Zephyr requirements
    (void)a; (void)b; (void)c;
    //Get the DHT22 from the Device Tree Source file
    const struct device *dht22 = DEVICE_DT_GET_ANY(aosong_dht);
    //Get the time of the next expected deadline
    int64_t next = k_uptime_get() + SENSOR_PERIOD_MS;
    for (uint32_t i = 0; i < SENSOR_TOTAL_SAMPLES; i++) {
        //Iterate deadline total
        _deadline_total++;
        //Get time and if its past the expected deadline + slack iterate miss counters
        if (k_uptime_get() > next + (DEADLINE_SLACK_US / 1000U))
            { _deadline_misses++; _sensor_missed++; }
        //Update expected deadline
        next += SENSOR_PERIOD_MS;
        
        //sensor_sample_fetch returns 0 if successful and a negative errno code
        // if failure
        if (sensor_sample_fetch(dht22) == 0) {
            //Initialize sensor_value structs to hold sensor readings
            struct sensor_value temp, hum;
            //Get temperature and humidity from DHT22 and iterate the number of sensor 
            // samples successfully collected
            sensor_channel_get(dht22, SENSOR_CHAN_AMBIENT_TEMP, &temp);
            sensor_channel_get(dht22, SENSOR_CHAN_HUMIDITY, &hum);
            _sensor_samples++;
        }
        //Sleep for SENSOR_PERIOD_MS
        k_msleep(SENSOR_PERIOD_MS);
    }
}

//Network thread
static void net_entry(void *a, void *b, void *c) {
    (void)a; (void)b; (void)c;
    //Initialize message and fill message with numbers
    uint8_t msg[NET_MSG_SIZE];
    for (uint32_t i = 0; i < NET_MSG_SIZE; i++) msg[i] = (uint8_t)i;

    //Initialize a socket to use IPv4, 
    int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    //Initialize the destination address as IPv4 using port 8080 
    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = zsock_htons(8080),
    };
    //Convert the a private IPv4 IP address 192.168.1.100 to a machine
    // readable format and store the result in dest_addr.sin_addr
    zsock_inet_pton(AF_INET, "192.168.1.100", &dest_addr.sin_addr);

    for (uint32_t i = 0; i < NET_MSG_COUNT; i++) {
        //Send the message over UDP using the socket defined by sock with
        // default behavior to the destination address
        int ret = zsock_sendto(sock, msg, NET_MSG_SIZE, 0, 
                              (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        //The integer ret is set to the number of bytes sent if no errors occured,
        // otherwise it is set to -1, if ret is greater than or equal to 0 iterate
        // the number of packets sent successfully, otherwise either the number of
        // packets lost 
        if (ret >= 0) _net_sent++;
        else _net_lost++;
        //Sleep for 5 milliseconds
        k_msleep(5U);
    }
    //Close the socket once execution finishes
    zsock_close(sock);
}

//Configuration file for interrupt
static struct counter_alarm_cfg _hf_alarm;
//Pointer to hardware timer
static const struct device     *_hf_ctr;

//Everytime an interrupt occurs, iterate the sucessful interrupt counter and
// reset the timer
static void hf_alarm_cb(const struct device *dev, uint8_t ch,
                         uint32_t ticks, void *ud) {
    (void)dev; (void)ch; (void)ud;
    _hf_irq_count++;
    _hf_alarm.ticks = counter_us_to_ticks(_hf_ctr, 1000000U / HF_IRQ_RATE_HZ);
    counter_set_channel_alarm(_hf_ctr, 0, &_hf_alarm);
}

static void hf_irq_stress_run(void) {
    //Get the expected number of interrupts
    uint32_t expected  = (HF_IRQ_RATE_HZ * HF_IRQ_DURATION_MS) / 1000U;
    //Set interrupt count to before test 0
    _hf_irq_count      = 0;
    //Get hardware timer 2
    _hf_ctr            = DEVICE_DT_GET(DT_NODELABEL(timer2));
    //Have hf_alarm_cb run when interrupt goes off and set hf.alarm.flags to 0 to
    // have the interrupt fire every _hf_alarm.ticks ticks
    _hf_alarm.callback = hf_alarm_cb;
    _hf_alarm.flags    = 0;
    //Get and set the number of hardware ticks between each interrupt
    _hf_alarm.ticks    = counter_us_to_ticks(_hf_ctr, 1000000U / HF_IRQ_RATE_HZ);
    //Start the timer
    counter_start(_hf_ctr);
    //Set the interrupt to monitor hardware timer 2 on channel 0 and operate on
    // the rules defined in _hf_alarm
    counter_set_channel_alarm(_hf_ctr, 0, &_hf_alarm);
    //Sleep for the duration of the interrupt test
    k_msleep(HF_IRQ_DURATION_MS);
    //Stop the timer after sleeping and calculate the number of misses if there
    // were any
    counter_stop(_hf_ctr);
    //Set the interrupt miss counter to 0
    _hf_irq_miss = 0;
    if (_hf_irq_count < expected) _hf_irq_miss = expected - _hf_irq_count;
}

//Dummy thread for concurrent tasks
static void dummy_entry(void *a, void *b, void *c) {
    (void)a; (void)b; (void)c;
    for (uint32_t i = 0; i < SENSOR_TOTAL_SAMPLES; i++) {
        k_msleep(SENSOR_PERIOD_MS);
    }
}

workload_results_t workload_run(void) {
    //Initialize metrics variables
    _sensor_samples = _sensor_missed = _net_sent = _net_lost = 0;
    _deadline_misses = _deadline_total = _hf_irq_count = 0;

    //Create threads for the sensor, network, and background tasks
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
                        dummy_entry, NULL, NULL, NULL, pri, 0, K_NO_WAIT);
    }

    //Sleep until the sensor task is finished
    k_thread_join(&_sens_td, K_FOREVER);
    //Run the interrupt stress test
    hf_irq_stress_run();

    //Set struct varaibles to global varaibles and return the struct
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
