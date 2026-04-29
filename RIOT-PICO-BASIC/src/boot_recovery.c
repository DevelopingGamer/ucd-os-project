//Library includes
#include "benchmark_common.h"
#include "boot_recovery.h"

//Set to the exact hardware cycle count the microcontroller receives power
volatile uint32_t g_reset_cycle_stamp = 0;

#define BKP_REG_PRE_RESET    0U
#define BKP_REG_RESET_CAUSE  1U
#define BENCH_RESET_MAGIC    0xDEADU

#if defined(FREERTOS_PICO)
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/vreg.h"
#include "hardware/powman.h"

//Get the current time
static uint32_t _get_cycle(void) {
    return (uint32_t)time_us_64();
}

//Time is already in microseconds, just return the value passed to it
static uint32_t _cycles_to_us(uint32_t cyc) {
    return cyc; 
}

//Write data to the PICO SDK watchdog scratch register with the
// index idx
static void _bkp_write(uint32_t idx, uint32_t val) {
    //Ensure idx is within the bounds of the 8 scratch registers
    if (idx < 8) {
        powman_hw->scratch[idx] = val;   // For Pico 2 / Pico 2W
    }
}

//Read data from the PICO SDK watchdog scratch register with
// the index idx
static uint32_t _bkp_read(uint32_t idx) {
    if (idx < 8) {
        return powman_hw->scratch[idx];
    }
    //Return 0 if idx was higher than the last valid scratch
    // register
    return 0;
}

//Trigger an intentional crash
static void _trigger_watchdog_reset(void) {
   //Enable the watchdog to reset the microcontroller in 1 millisecond
   // by passing a 1 as the first parameter and enable the pause_on_debug
   // boolean by passing a 1 as the second parameter to have the CPU pause
   // when the watchdog reaches 0 (pausing execution before the reset 
   // triggers to allow for debugging)
    watchdog_enable(1, 1); 
    
    //Forces the CPU to do nothing so it can't reset the watchdog
    // timer, making the watchdog reset the microcontroller 
    while(1) { __asm__("nop"); }
}

#elif defined(RIOT_OS)
#include "periph/pm.h"
#include "ztimer.h"

//Return the current time in microseconds
static uint32_t _get_cycle(void)             { return ztimer_now(ZTIMER_USEC); }
//Time is already in microseconds, just return the value passed to it
static uint32_t _cycles_to_us(uint32_t v)    { return v; }
//bkp read and write are STM32 only, due to the ESP32 and Raspberry Pi Pico 2W lacking
// these functions they aren't present in RIOT
static void     _bkp_write(uint32_t i, uint32_t v) { (void)i; (void)v; }
static uint32_t _bkp_read(uint32_t i)        { (void)i; return 0; }

/*
static void _trigger_watchdog_reset(void) {
    //Configure watchdog to trigger a reboot in the minimum possible time by
    // setting the minimum time to 0ms but the maximum time to 1ms and then
    // getting the CPU stuck in an infinite loop
    wdt_setup_reboot(0, 1);
    //Start the countdown
    wdt_start();
    //Infinite loop until reboot
    while (1) { __asm__("nop"); }
}
*/
static void _trigger_watchdog_reset(void) {
    //The RP2040 port of RIOT OS lacks a hardware watchdog driver.
    //Simulate the reboot using ztimer followed by a software reboot
    ztimer_sleep(ZTIMER_MSEC, 1000);
    pm_reboot();
}

#elif defined(ZEPHYR)
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/timing/timing.h>

//Get the current time
static uint32_t _get_cycle(void) { return (uint32_t)timing_counter_get(); }
//Convert the current time to nanoseconds and then microseconds and then return
// the time in microseconds
static uint32_t _cycles_to_us(uint32_t cyc) {
    return (uint32_t)timing_cycles_to_ns(cyc) / 1000U;
}

static void _bkp_write(uint32_t idx, uint32_t val) {
    //Use the Device Tree Source file to find the microcontrollers retram
    const struct device *ret = DEVICE_DT_GET(DT_NODELABEL(retram));
    //Write retained data to ret, starting at idx for 4 bytes, with the data
    // passed into it as an array of 1 byte values, with the data being 4 bytes
    retained_mem_write(ret, idx * 4U, (uint8_t *)&val, 4U);
}


static uint32_t _bkp_read(uint32_t idx) {
    //Initialize v to 0
    uint32_t v = 0;
    //Use the Device Tree Source file to find the microcontrollers retram
    const struct device *ret = DEVICE_DT_GET(DT_NODELABEL(retram));
    //Read retained data from ret, starting at idx for 4 bytes, with the function
    // reading v as an array of 1 byte values, with the data being read being 4 bytes
    retained_mem_read(ret, idx * 4U, (uint8_t *)&v, 4U);
    //Return the data read
    return v;
}


static void _trigger_watchdog_reset(void) {
    //Get watchdog device from Device Source Tree file
    const struct device *wdt = DEVICE_DT_GET(DT_NODELABEL(iwdg));
    //Set min and max time to 0 and 1 respectively just like with RIOT,
    // don't have the watchdog save any data before rebooting, and
    // set the watchdog to reset the Entire System-on-Chip
    struct wdt_timeout_cfg cfg = {
        .window.min = 0U, .window.max = 1U,
        .callback = NULL, .flags = WDT_FLAG_RESET_SOC,
    };
    //Loads the watchdog configuration to a specific watchdog channel
    // stored in ch
    int ch = wdt_install_timeout(wdt, &cfg);
    //Stops watchdog if CPU is paused during debugging to prevent reset
    wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
    //Starts the 1 ms timer
    wdt_feed(wdt, ch);
    //Infinite loop to keep the CPU busy before the watchdog reboots
    // the system
    while (1) {}
}
#endif

//Get the boot time once and then return it for all subsequent runs
static uint32_t _cached_boot_ms = 0;
static bool _boot_latched = false;

uint32_t boot_measure_time_ms(void) {
    if (!_boot_latched) {
        // Calculate the boot time once and freeze it in memory
        uint32_t delta = _get_cycle() - g_reset_cycle_stamp;
        _cached_boot_ms = _cycles_to_us(delta) / 1000U;
        _boot_latched = true;
    }
    
    //Return the cached boot time
    return _cached_boot_ms;
}
//Ran to reset microcontroller and immediately after resetting,
// pass arm_reset as true to reset and pass arm_reset as false
// immediately after reset
uint32_t boot_measure_recovery_ms(bool arm_reset) {
    //To reset, write the current time to backup memory,
    // write 0xDEADU to backup memory to show that the
    // reset was intentional, and trigger the reset (
    // return 0 is never reached)
    if (arm_reset) {
        _bkp_write(BKP_REG_PRE_RESET,   _get_cycle());
        _bkp_write(BKP_REG_RESET_CAUSE, BENCH_RESET_MAGIC);
        _trigger_watchdog_reset();
        return 0;
    }
    //The code only ever gets this far when arm_reset is false, if the reset
    // was caused by something else immediately return 0, if the reset was
    // caused by a previous boot_measure_recovery_ms call with arm_reset set
    // to true then read the pre-reset time from backup memory and get the current
    // time, then reset the BKP_REG_RESET_CAUSE register from 0xDEADU to 0U and
    // return the difference in milliseconds
    if (_bkp_read(BKP_REG_RESET_CAUSE) != BENCH_RESET_MAGIC) return 0;
    uint32_t pre  = _bkp_read(BKP_REG_PRE_RESET);
    uint32_t post = _get_cycle();
    _bkp_write(BKP_REG_RESET_CAUSE, 0U);
    return _cycles_to_us(post - pre) / 1000U;
}
