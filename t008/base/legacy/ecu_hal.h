/* ecu_hal.h – platform services the brake task builds on.
 *
 * This is the ECU vendor's HAL. Treat the *contract* documented here as fixed:
 * it describes how the real hardware/driver layer behaves, and your harness must
 * reproduce these semantics. You may reimplement it for the host, but you may not
 * weaken a contract in order to make a defect disappear.
 */
#ifndef ECU_HAL_H
#define ECU_HAL_H

#include <stdint.h>

/* A raw CAN frame as delivered by the driver. */
typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
} can_msg_t;

/* Drive the pressure modulator. duty is the setpoint in mbar, 0..10000.
 * Real time path: returns immediately, never blocks. */
void pwm_set_duty(uint16_t duty_mbar);

/* Asynchronous diagnostic logging.
 *
 * CONTRACT: this call is non-blocking. It does NOT copy the string – it stores
 * the caller's pointer in an internal ring buffer and returns. The stored
 * pointers are dereferenced and formatted out later, from the FreeRTOS idle
 * hook (see diag_log_flush). The caller must therefore guarantee that the
 * pointed-to storage stays alive and unmodified until the flush has happened.
 */
void diag_log_async(const char *msg);

/* Called from the idle hook: dereferences and emits everything queued by
 * diag_log_async since the last flush. */
void diag_log_flush(void);

/* Test/harness accessors ------------------------------------------------- */

/* Last value handed to pwm_set_duty(). */
uint16_t hal_last_duty(void);

/* Number of log lines emitted by diag_log_flush() so far, and a copy of the
 * most recently emitted line (as it was observed at flush time). */
uint32_t     hal_log_count(void);
const char  *hal_last_flushed_line(void);

#endif /* ECU_HAL_H */
