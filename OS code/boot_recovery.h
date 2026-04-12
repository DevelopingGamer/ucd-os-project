#ifndef BOOT_RECOVERY_H
#define BOOT_RECOVERY_H

#include <stdint.h>
#include <stdbool.h>

/*
 * boot_recovery.h
 *
 * KPIs:
 *   - Boot time:     hardware reset → first user task scheduled
 *   - Recovery time: watchdog-induced reset → task ready again
 */

extern volatile uint32_t g_reset_cycle_stamp;

uint32_t boot_measure_time_ms(void);
uint32_t boot_measure_recovery_ms(bool arm_reset);

#endif /* BOOT_RECOVERY_H */
