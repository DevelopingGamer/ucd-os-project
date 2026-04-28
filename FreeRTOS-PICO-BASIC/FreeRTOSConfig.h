#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* Hardware specifics */
#define configCPU_CLOCK_HZ                      150000000 /* Pico 2W default clock */
#define configTICK_RATE_HZ                      1000      /* Mandatory for SENSOR_PERIOD_MS math */
#define configMAX_PRIORITIES                    7
#define configMAX_TASK_NAME_LEN                 16
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS

/* Memory allocation */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#define configTOTAL_HEAP_SIZE                   (128 * 1024) /* 128 KB for the benchmark */

/* Required by the Pico SDK */
#define configUSE_TICKLESS_IDLE                 0
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_IDLE_HOOK                     1 /* Added for cpu_monitor.c */
#define configUSE_TICK_HOOK                     1 /* Added for cpu_monitor.c */

/* Benchmarking Specific Flags */
#define configUSE_TRACE_FACILITY                1 /* Enables vPortGetHeapStats */
#define configGENERATE_RUN_TIME_STATS           0 /* Handled manually by cpu_monitor.c */
#define INCLUDE_uxTaskGetStackHighWaterMark     1 /* For mem_profile.c */
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_xTaskGetTickCount               1

/* Pico SDK Interrupt Routing */
#define SVC_Handler                             isr_svcall
#define PendSV_Handler                          isr_pendsv
#define SysTick_Handler                         isr_systick

/* ARM Cortex-M33 Specific Hardware Flags (Pico 2W / RP2350) */
#define configENABLE_FPU                        1 /* Pico 2W has a hardware FPU */
#define configENABLE_MPU                        0 /* Memory Protection Unit not used */
#define configENABLE_TRUSTZONE                  0 /* TrustZone security disabled */
#define configRUN_FREERTOS_SECURE_ONLY          1 /* Required when TrustZone is 0 */
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 256 )

/* ARM Cortex-M33 Interrupt Priorities */
#define configPRIO_BITS                         4 /* 15 priority levels on RP2350 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY         ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/*Chcek for Stack Overflows*/
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#endif /* FREERTOS_CONFIG_H */