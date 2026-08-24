/* ecu_hal.c – host-side stand-in for the ECU vendor HAL.
 *
 * Behaviourally faithful to the contract in ecu_hal.h: diag_log_async stores the
 * caller's *pointer*, never a copy, and the flush happens later. Keep it that way
 * – copying the string here would weaken the contract the real driver imposes.
 */
#include "ecu_hal.h"

#include <stdio.h>
#include <string.h>

#define DIAG_RING_LEN 32u

static volatile uint16_t s_last_duty;

static const char *s_ring[DIAG_RING_LEN];
static uint32_t    s_head;
static uint32_t    s_tail;
static uint32_t    s_log_count;
static char        s_last_line[64];

void pwm_set_duty(uint16_t duty_mbar)
{
    s_last_duty = duty_mbar;
}

uint16_t hal_last_duty(void)
{
    return s_last_duty;
}

void diag_log_async(const char *msg)
{
    uint32_t next = (s_head + 1u) % DIAG_RING_LEN;
    if (next == s_tail) {
        return;                 /* ring full: drop, as the real driver does */
    }
    s_ring[s_head] = msg;       /* pointer only – no copy, by design */
    s_head = next;
}

void diag_log_flush(void)
{
    while (s_tail != s_head) {
        const char *msg = s_ring[s_tail];
        s_tail = (s_tail + 1u) % DIAG_RING_LEN;
        if (msg == NULL) {
            continue;
        }
        snprintf(s_last_line, sizeof s_last_line, "%s", msg);
        s_log_count++;
    }
}

uint32_t hal_log_count(void)
{
    return s_log_count;
}

const char *hal_last_flushed_line(void)
{
    return s_last_line;
}
