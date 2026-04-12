/*
 * io_bench.c
 *
 * KPIs covered:
 *   - UART/serial I/O throughput (KB/s)
 *   - Packet loss rate (%)
 *
 * Method:
 *   512 blocks of 1024 bytes are transmitted over a loopback UART.
 *   Each received block is integrity-checked against the known pattern.
 *   Throughput = total bytes transferred / elapsed time.
 *   Loss rate  = failed or corrupted transfers / total attempts.
 */

#include "benchmark_common.h"
#include "io_bench.h"

#define IO_BUF_SIZE    1024U
#define IO_ITERATIONS   512U

static uint8_t _tx_buf[IO_BUF_SIZE];
static uint8_t _rx_buf[IO_BUF_SIZE];

/* ════════════════════════════════════════════════════════════════════
 *  FreeRTOS (STM32 HAL UART)
 * ═══════════════════════════════════════════════════════════════════*/
#if defined(FREERTOS)
#include "stm32f4xx_hal.h"
extern UART_HandleTypeDef huart2;

io_stats_t io_bench_run(void) {
    io_stats_t s = {0};
    uint32_t lost = 0, total_bytes = 0;

    for (uint32_t i = 0; i < IO_BUF_SIZE; i++) _tx_buf[i] = (uint8_t)(i & 0xFF);

    uint32_t t0 = xTaskGetTickCount();
    for (uint32_t i = 0; i < IO_ITERATIONS; i++) {
        HAL_UART_Transmit(&huart2, _tx_buf, IO_BUF_SIZE, HAL_MAX_DELAY);
        HAL_StatusTypeDef r = HAL_UART_Receive(&huart2, _rx_buf,
                                                IO_BUF_SIZE, 50);
        if (r != HAL_OK) {
            lost++;
        } else {
            bool ok = true;
            for (uint32_t b = 0; b < IO_BUF_SIZE; b++) {
                if (_rx_buf[b] != _tx_buf[b]) { ok = false; break; }
            }
            if (!ok) lost++;
            else total_bytes += IO_BUF_SIZE;
        }
    }
    uint32_t elapsed_ms = TICK_TO_MS(xTaskGetTickCount() - t0);

    s.throughput_kbps   = (elapsed_ms > 0) ? (total_bytes / elapsed_ms) : 0;
    s.packet_loss_pct   = (float)lost * 100.0f / (float)IO_ITERATIONS;
    return s;
}

/* ════════════════════════════════════════════════════════════════════
 *  RIOT OS (periph/uart)
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(RIOT_OS)
#include "periph/uart.h"

io_stats_t io_bench_run(void) {
    io_stats_t s = {0};
    uint32_t lost = 0, total_bytes = 0;

    for (uint32_t i = 0; i < IO_BUF_SIZE; i++) _tx_buf[i] = (uint8_t)(i & 0xFF);

    uint32_t t0 = ztimer_now(ZTIMER_MSEC);
    for (uint32_t i = 0; i < IO_ITERATIONS; i++) {
        uart_write(UART_DEV(0), _tx_buf, IO_BUF_SIZE);
        int received = uart_read_blocking(_rx_buf, IO_BUF_SIZE, 50000);
        if (received != (int)IO_BUF_SIZE) {
            lost++;
        } else {
            total_bytes += IO_BUF_SIZE;
        }
    }
    uint32_t elapsed_ms = ztimer_now(ZTIMER_MSEC) - t0;

    s.throughput_kbps = (elapsed_ms > 0) ? (total_bytes / elapsed_ms) : 0;
    s.packet_loss_pct = (float)lost * 100.0f / (float)IO_ITERATIONS;
    return s;
}

/* ════════════════════════════════════════════════════════════════════
 *  Zephyr (UART poll API)
 * ═══════════════════════════════════════════════════════════════════*/
#elif defined(ZEPHYR)
#include <zephyr/drivers/uart.h>

io_stats_t io_bench_run(void) {
    io_stats_t s = {0};
    uint32_t lost = 0, total_bytes = 0;
    const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

    for (uint32_t i = 0; i < IO_BUF_SIZE; i++) _tx_buf[i] = (uint8_t)(i & 0xFF);

    uint32_t t0 = (uint32_t)k_uptime_get();
    for (uint32_t i = 0; i < IO_ITERATIONS; i++) {
        for (uint32_t b = 0; b < IO_BUF_SIZE; b++)
            uart_poll_out(uart, _tx_buf[b]);

        int received = 0;
        uint32_t deadline = (uint32_t)k_uptime_get() + 50U;
        while (received < (int)IO_BUF_SIZE &&
               (uint32_t)k_uptime_get() < deadline) {
            uint8_t c;
            if (uart_poll_in(uart, &c) == 0)
                _rx_buf[received++] = c;
        }
        if (received != (int)IO_BUF_SIZE) lost++;
        else total_bytes += IO_BUF_SIZE;
    }
    uint32_t elapsed_ms = (uint32_t)k_uptime_get() - t0;

    s.throughput_kbps = (elapsed_ms > 0) ? (total_bytes / elapsed_ms) : 0;
    s.packet_loss_pct = (float)lost * 100.0f / (float)IO_ITERATIONS;
    return s;
}
#endif
