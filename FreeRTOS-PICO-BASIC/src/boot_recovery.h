//Include guard
#ifndef BOOT_RECOVERY_H
#define BOOT_RECOVERY_H
//Library imports
#include <stdint.h>
#include <stdbool.h>
//Variable defined in boot_recovery.c
extern volatile uint32_t g_reset_cycle_stamp;
//Function prototypes
uint32_t boot_measure_time_ms(void);
uint32_t boot_measure_recovery_ms(bool arm_reset);
//Include guard
#endif
