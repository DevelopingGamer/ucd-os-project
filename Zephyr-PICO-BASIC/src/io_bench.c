#include "benchmark_common.h"
#include "io_bench.h"

//Set the buffer size to a kilobyte and
// send over the data 100 times
#define IO_BUF_SIZE    1024U
#define IO_ITERATIONS   100U

//Initialze the tx and rx buffers to a kilobyte
// each
static uint8_t _tx_buf[IO_BUF_SIZE];
static uint8_t _rx_buf[IO_BUF_SIZE];

#if defined(FREERTOS_PICO)
#include "pico/stdlib.h"
#include "hardware/uart.h"

//Define output pin for UART communication 
#define BENCH_UART uart0

io_stats_t io_bench_run(void) {
    //Initialize struct values to 0
    io_stats_t s = {0};
    //Initialize lost packages and total_bytes to0
    uint32_t lost = 0, total_bytes = 0;
    //Fill the transmission buffer with the last 8 bits of i for each i
    for (uint32_t i = 0; i < IO_BUF_SIZE; i++) _tx_buf[i] = (uint8_t)(i & 0xFF);
    //Get the time before transmission
    uint32_t t0 = xTaskGetTickCount();

    for (uint32_t i = 0; i < IO_ITERATIONS; i++) {
        //Initialize the number of sent bytes to 0
        uint32_t sent = 0;
        //Initialize the number of received bytes to 0
        uint32_t received = 0;
        //Get the time before receiving data
        uint32_t loop_start = xTaskGetTickCount();    
        //While there is still data to read and write and the 100 ms time out hasn't expired
        while (received < IO_BUF_SIZE && (TICK_TO_MS(xTaskGetTickCount() - loop_start) < 100U)) {
            //If there is still data to send and the TX hardware has room, push a byte
            if (sent < IO_BUF_SIZE && uart_is_writable(BENCH_UART)) {
                uart_get_hw(BENCH_UART)->dr = _tx_buf[sent++];
            }
            //If there is data to read, pull a byte
            if (uart_is_readable(BENCH_UART)) {
                _rx_buf[received++] = uart_getc(BENCH_UART);
            }
        }
        //If the number of bytes received doesn't match the number of bytes sent
        // iterate the number of packets lost
        if (received != IO_BUF_SIZE) {
            lost++;        } else {
            //Check if the bytes received match the bytes sent
            bool ok = true;
            for (uint32_t b = 0; b < IO_BUF_SIZE; b++) {
                if (_rx_buf[b] != _tx_buf[b]) { ok = false; break; }
            }
            //If any of the bytes received don't match the bytes sent iterate the
            // number of lost packets
            if (!ok) lost++;
            //If the bytes received match the bytes sent increase the total number
            // of bytes successfully sent
            else total_bytes += IO_BUF_SIZE;
        }
    }
    
    //Get the time the UART communication took
    uint32_t elapsed_ms = TICK_TO_MS(xTaskGetTickCount() - t0);
    //If the elapsed time is greater than 0 get the throughout by dividing the number 
    // of bytes sucessfully sent by the amount of time the UART communication took,
    // if the elapsed time is 0 set the throughout to 0
    s.throughput_kbps   = (elapsed_ms > 0) ? (total_bytes / elapsed_ms) : 0;
    //Get the percentage of packets lost by multiplying the number of packets lost
    // by 100 and dividing it by the number of packets sent
    s.packet_loss_pct   = (float)lost * 100.0f / (float)IO_ITERATIONS;
    //Return the metrics struct
    return s;
}

#elif defined(RIOT_OS)
#include "periph/uart.h"
#include "thread.h"

//Static integer used to store the number of bytes received
// from UART 
static volatile uint32_t _riot_rx_received = 0;

//Runs whenever a byte is received, adds byte to _rx_buf
static void riot_uart_rx_cb(void *arg, uint8_t data) {
    (void)arg;
    if (_riot_rx_received < IO_BUF_SIZE) {
        _rx_buf[_riot_rx_received++] = data;
    }
}

io_stats_t io_bench_run(void) {
    //Initialize the struct and the lost and total_bytes values
    io_stats_t s = {0};
    uint32_t lost = 0, total_bytes = 0;

    //Fill the tx buffer with the last 8 bits of i for each i
    for (uint32_t i = 0; i < IO_BUF_SIZE; i++) _tx_buf[i] = (uint8_t)(i & 0xFF);
    //Initialize UART communication to use GP0 and GP1, with the project's baud rate,
    // using the UART interrupt defined above, with no values passed to the callback
    // function
    uart_init(UART_DEV(0), 115200, riot_uart_rx_cb, NULL);
    //Get the timer
    uint32_t t0 = ztimer_now(ZTIMER_MSEC);
    for (uint32_t i = 0; i < IO_ITERATIONS; i++) {
        //Initialize sent and reset the static received bytes to 0
        uint32_t sent = 0;
        _riot_rx_received = 0;
        //Get time for loop timeout
        uint32_t loop_start = ztimer_now(ZTIMER_MSEC);
        //Loop through writing and reading each byte with 100 ms timeout
        while (_riot_rx_received < IO_BUF_SIZE && (ztimer_now(ZTIMER_MSEC) - loop_start < 100U)) {
            //Push a byte
            if (sent < IO_BUF_SIZE) {
                uart_write(UART_DEV(0), &_tx_buf[sent++], 1);
            }
            //Yield to allow interrupt to pull byte
            thread_yield();
        }
        //If all bytes weren't received iterate lost, otherwise add bytes received to total_bytes 
        if (_riot_rx_received != (int)IO_BUF_SIZE) {
            lost++;
        } else {
            total_bytes += IO_BUF_SIZE;
        }
    }
    //Get the time the UART communication took
    uint32_t elapsed_ms = ztimer_now(ZTIMER_MSEC) - t0;
    //Get the throughout and lost packets
    s.throughput_kbps = (elapsed_ms > 0) ? (total_bytes / elapsed_ms) : 0;
    s.packet_loss_pct = (float)lost * 100.0f / (float)IO_ITERATIONS;
    return s;
}

#elif defined(ZEPHYR)
#include <zephyr/drivers/uart.h>

io_stats_t io_bench_run(void) {
    //Initialize struct values to 0
    io_stats_t s = {0};
    //Set lost and total_bytes to 0
    uint32_t lost = 0, total_bytes = 0;
    //Get uart device from the Device Tree Source file
    const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart0));
    //Fill the tx buffer with the last 8 bits of i for each i
    for (uint32_t i = 0; i < IO_BUF_SIZE; i++) _tx_buf[i] = (uint8_t)(i & 0xFF);
    //Get the current time
    uint32_t t0 = (uint32_t)k_uptime_get();
    
    for (uint32_t i = 0; i < IO_ITERATIONS; i++) {
        int received = 0;
        uint32_t deadline = (uint32_t)k_uptime_get() + 250U;
        
        //Send and receive byte-by-byte to prevent 32-byte FIFO overflow
        for (uint32_t b = 0; b < IO_BUF_SIZE; b++) {
            uart_poll_out(uart, _tx_buf[b]);
            
            uint8_t c;
            //Wait for single bit to arrive
            while (uart_poll_in(uart, &c) != 0) {
                if ((uint32_t)k_uptime_get() >= deadline) break;
            }
            
            //If save if bit obtained before the deadline
            if ((uint32_t)k_uptime_get() < deadline) {
                _rx_buf[received++] = c;
            }
        }
        //Iterate lost packages if data is incomplete, otherwise add
        // bytes obtained
        if (received != (int)IO_BUF_SIZE) lost++;
        else total_bytes += IO_BUF_SIZE;
    }
    //Get the elapsed time while doing uart communication
    uint32_t elapsed_ms = (uint32_t)k_uptime_get() - t0;
    //Get the throughput and the packet loss
    s.throughput_kbps = (elapsed_ms > 0) ? (total_bytes / elapsed_ms) : 0;
    s.packet_loss_pct = (float)lost * 100.0f / (float)IO_ITERATIONS;
    return s;
}
#endif
