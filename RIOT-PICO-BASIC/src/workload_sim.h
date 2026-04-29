//Include guard
#ifndef WORKLOAD_SIM_H
#define WORKLOAD_SIM_H

//Included header files
#include "benchmark_common.h"

//Define how often the simulated sensor task should
// wake up, how many samples the sensor task should
// attempt to collect, how many messages the
// network task should send, how many bytes each
// message should be, how many additional
// concurrent tasks should be ran for load testing,
// how many times interrupts should be done per
// second for load testing, how long the interrupt
// load test should last, how many times the load
// test should run, and how much slack the sensor
// task should have (ex. with 500ms slack if the
// sensor task wakes up 500ms after it was 
// suppose to it'll pass but not if it woke up
// 501ms after it was suppose to)
#define SENSOR_PERIOD_MS      25U
#define SENSOR_TOTAL_SAMPLES  1000U
#define NET_MSG_COUNT         200U
#define NET_MSG_SIZE          64U
#define CONCURRENT_TASKS      8U
#define HF_IRQ_RATE_HZ        5000U
#define HF_IRQ_DURATION_MS    2000U
#define STRESS_REPEAT_RUNS    5U
#define DEADLINE_SLACK_US     500U

//Function prototype for workload_run
workload_results_t workload_run(void);

//Include guard
#endif
