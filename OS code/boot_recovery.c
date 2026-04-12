/*
 * boot_recovery.c
 *
 * KPIs covered:
 *   - Boot time:     hardware reset -> first user task scheduled
 *   - Recovery time: watchdog-induced reset -> task ready again
 *
 * Method:
 *   ARM DWT cycle counter is latched at reset (g_reset_cycle_stamp).
 *   boot_measure_time_ms() computes elapsed cycles from that stamp to
 *   the moment the benchmark task first calls it.
 *
 *   Recovery: a pre-reset timestamp is stored in a battery-backed
 *   register. After the watchdog trips and the board reboots,
 *   boot_measure_recovery_ms(false) reads the stamp and computes delta.
 *
 * Populate g_reset_cycle_stamp in startup_stm32xxx.s or SystemInit():
 *   CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
 *   DWT->CYCCNT       = 0;
 *   DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
 *   g_reset_cycle_stamp = DWT->CYCCNT;
 */

#include "benchmark_common.h"
#include "boot_recovery.h"

volatile uint32_t g_reset_cycle_stamp = 0;

#define BKP_REG_PRE_RESET    0U
#define BKP_REG_RESET_CAUSE  1U
#define BENCH_RESET_MAGIC    0xDEADU

/* ════════════════════════════════════════════════════════════════════
 *  FreeRTOS (STM32 HAL)
 * ═══════════════════════════════════════════════════════════════════*/
#if defined(FREERTOS)
#include "stm32f4xx_hal.h"
extern RTC_HandleTypeDef hrtc;

static uint32_t _get_cycle(void) {
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT       = 0;
        DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
    }
    return DWT->CYCCNT;
}
static uint32_t _cycles_to_us(uint32_t cyc) {
    return cyc / (SystemCoreClock / 1000000UL);
}
static void _bkp_write(uint32_t idx, uint32_t val) {
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, idx, val);
}
static uint32_t _bkp_read(uint32_t idx) {
    return HAL_RTCEx_BKUPRead(&hrtc, idx);
}
static void _trigger_watchdog_reset(void) {
    IWDG->KR  = 0x5555U;
    IWDG->PR  = 0U;
    IWDG->RLR = 1U;
    IWDG->KR  = 0xCCCCU;
    while (1) { __NOP(); }
}

/* ════════════════════════════════════════════════════════════════════
 *  RIOT OS
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(RIOT_OS)
#include "periph/wdt.h"

static uint32_t _get_cycle(void)             { return ztimer_now(ZTIMER_USEC); }
static uint32_t _cycles_to_us(uint32_t v)    { return v; }
static void     _bkp_write(uint32_t i, uint32_t v) { (void)i; (void)v; }
static uint32_t _bkp_read(uint32_t i)        { (void)i; return 0; }
static void _trigger_watchdog_reset(void) {
    wdt_setup_reboot(0, 1);
    wdt_start();
    while (1) { __asm__("nop"); }
}

/* ════════════════════════════════════════════════════════════════════
 *  Zephyr
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(ZEPHYR)
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/timing/timing.h>

static uint32_t _get_cycle(void) { return (uint32_t)timing_counter_get(); }
static uint32_t _cycles_to_us(uint32_t cyc) {
    return (uint32_t)timing_cycles_to_ns(cyc) / 1000U;
}
static void _bkp_write(uint32_t idx, uint32_t val) {
    const struct device *ret = DEVICE_DT_GET(DT_NODELABEL(retram));
    retained_mem_write(ret, idx * 4U, (uint8_t *)&val, 4U);
}
static uint32_t _bkp_read(uint32_t idx) {
    uint32_t v = 0;
    const struct device *ret = DEVICE_DT_GET(DT_NODELABEL(retram));
    retained_mem_read(ret, idx * 4U, (uint8_t *)&v, 4U);
    return v;
}
static void _trigger_watchdog_reset(void) {
    const struct device *wdt = DEVICE_DT_GET(DT_NODELABEL(iwdg));
    struct wdt_timeout_cfg cfg = {
        .window.min = 0U, .window.max = 1U,
        .callback = NULL, .flags = WDT_FLAG_RESET_SOC,
    };
    int ch = wdt_install_timeout(wdt, &cfg);
    wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
    wdt_feed(wdt, ch);
    while (1) {}
}
#endif

/* ════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════*/
uint32_t boot_measure_time_ms(void) {
    uint32_t delta = _get_cycle() - g_reset_cycle_stamp;
    return _cycles_to_us(delta) / 1000U;
}

uint32_t boot_measure_recovery_ms(bool arm_reset) {
    if (arm_reset) {
        _bkp_write(BKP_REG_PRE_RESET,   _get_cycle());
        _bkp_write(BKP_REG_RESET_CAUSE, BENCH_RESET_MAGIC);
        _trigger_watchdog_reset();
        return 0; /* never reached */
    }
    if (_bkp_read(BKP_REG_RESET_CAUSE) != BENCH_RESET_MAGIC) return 0;
    uint32_t pre  = _bkp_read(BKP_REG_PRE_RESET);
    uint32_t post = _get_cycle();
    _bkp_write(BKP_REG_RESET_CAUSE, 0U);
    return _cycles_to_us(post - pre) / 1000U;
}
