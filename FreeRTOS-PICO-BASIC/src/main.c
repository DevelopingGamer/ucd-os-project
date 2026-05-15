//Compile by starting in build directory, then run cmake .. and then make -j4
#include "benchmark_common.h"
#include "bench_log.h"
#include "boot_recovery.h"
#include "cpu_monitor.h"
#include "latency_meas.h"
#include "mem_profile.h"
#include "io_bench.h"
#include "fs_bench.h"
#include "workload_sim.h"
#include <string.h>
#include "pico/cyw43_arch.h"
#include "hardware/irq.h"

//Define the number of times the benchmark tests should be ran
#define TOTAL_BENCHMARK_RUNS 250

#if defined(FREERTOS_PICO)
//Include the pico's stdlib for printf, timers and delays,
// and basic pin control for GPIO pins
#include "pico/stdlib.h"
#endif

void bench_init(void) {
#if defined(FREERTOS_PICO)
    //Initialize UART and set the baud rate
    uart_init(uart0, 115200);
    
    //Set the physical pins for UART
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
#endif
}

void bench_run_all(bench_results_t *out, uint32_t run_idx) {
    //Set all out (bench_results_t struct) values to 0
    memset(out, 0, sizeof(bench_results_t));
    //Set the run_index value to the current run ith run (passed from execute_benchmark_suite)
    out->run_index = run_idx;
    //Copy RTOS_NAME to platform variable in out, do - 1 after the sizeof call to account for
    // the null terminator
    strncpy(out->platform, RTOS_NAME, sizeof(out->platform) - 1);

    //Run boot_recovery.c before other threads start running
    out->boot_time_ms = boot_measure_time_ms();
    out->stability.recovery_ms = boot_measure_recovery_ms(false);

    //Run latency_meas.c
    //Does not work alongside fs_bench.c in FreeRTOS
    //out->latency = latency_measure_all();

    //Run io_bench.c
    io_stats_t io_res = io_bench_run();
    out->io.uart_throughput_kbps = io_res.throughput_kbps;
    out->io.uart_packet_loss_pct = io_res.packet_loss_pct;

    //Run fs_bench.c
    //Does not work alongside latency_meas.c in FreeRTOS
    fs_stats_t fs_res = fs_bench_run();
    out->io.fs_write_kbps = fs_res.fs_write_kbps;
    out->io.fs_read_kbps  = fs_res.fs_read_kbps;
    out->io.fs_sync_us    = fs_res.fs_sync_us;

    //Run workload_sim.c
    out->workload = workload_run();

    //Run cpu_monitor.c
    out->cpu = cpu_monitor_sample();
    out->mem = mem_profile_collect();
}

static void execute_benchmark_suite(void) {
    bench_init();
    
    //Print the CSV header before looping
    bench_log_csv_header();
    //Delay to allow header to print
    vTaskDelay(pdMS_TO_TICKS(100));

    //Run the benchmark tests TOTAL_BENCHMARK_RUNS times
    for (uint32_t i = 1; i <= TOTAL_BENCHMARK_RUNS; i++) {
        bench_results_t current_run;
        bench_run_all(&current_run, i);

        //Output the human-readable text and the CSV row to UART
        //bench_log_results(&current_run);
        bench_log_csv_row(&current_run);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

#if defined(FREERTOS_PICO)

//Dedicated polling task to prevent the interrupt problems
static void wifi_poll_task(void *arg) {
    (void)arg;
    while (1) {
        cyw43_arch_poll();
        //Yield the CPU to benchmarks, wake up every 10ms to clear Wi-Fi interrupts
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void main_task(void *arg) {
    (void)arg;
    printf("\n--- FreeRTOS Scheduler Started ---\n");
    
    //Initialize Wi-Fi safely inside OS boundary
    if (cyw43_arch_init() == 0) {
        cyw43_arch_enable_sta_mode();
        vTaskDelay(pdMS_TO_TICKS(1000)); //Use FreeRTOS delay instead of sleep_ms in FreeRTOS thread
        
        uint8_t mac[6];
        cyw43_hal_get_mac(0, mac);
        printf("Hardware MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            
        printf("Attempting to connect to Wi-Fi\n");
        cyw43_arch_wifi_connect_async("PicoTest", "8wQ9h0T63fhet910", CYW43_AUTH_WPA2_AES_PSK);
        
        int retry_limit = 2000; 
        while(retry_limit > 0) {
            cyw43_arch_poll();
            
            int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
            if (status == CYW43_LINK_UP) {
                printf("Connected to Wi-Fi successfully\n");
                
                // Launch the background polling task immediately upon connection
                xTaskCreate(wifi_poll_task, "POLL", 512, NULL, tskIDLE_PRIORITY + 2, NULL);
                break;
            }
            
            if (status < 0) {
                printf("Hardware Error: %d\n", status);
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(10)); // OS-safe delay
            retry_limit--;
            
            if (retry_limit % 100 == 0) printf("Retrying Wi-Fi connection\n");
        }
        if (retry_limit == 0) {
            printf("Wi-Fi connection failed due to time out\n");
        }
    }
    
    // One second delay for UART stability
    vTaskDelay(pdMS_TO_TICKS(1000));
    execute_benchmark_suite();
    
    vTaskDelete(NULL);
}

int main(void) {
    // Initialize standard IO hardware
    stdio_init_all();
    
    // Give the USB Serial Monitor 600 milliseconds to establish a connection to the PC
    sleep_ms(600); 
    printf("\nBooting Pico 2W...\n");
    //Ensure crash is happening during FreeRTOS kernel startup during applicable
    // debugging situation
    printf("Starting Scheduler...\n");
    stdio_flush();

    //Launch FreeRTOS immediately on quiet hardware
    xTaskCreate(main_task, "MAIN", 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskStartScheduler();
    
    return 0;
}

//Check for Stack Overflows
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    printf("\nFATAL: Stack overflow in task: %s\n", pcTaskName);
    while(1);
}
void vApplicationMallocFailedHook(void) {
    printf("\nFATAL: FreeRTOS Heap exhausted (Malloc Failed)\n");
    while(1);
}

//If using RIOT or Zephyr, a main thread will be created automatically that can be used to
// call execute_benchmark_suite
#elif defined(RIOT_OS) || defined(ZEPHYR)

int main(void) {
    execute_benchmark_suite();
    return 0;
}

#endif