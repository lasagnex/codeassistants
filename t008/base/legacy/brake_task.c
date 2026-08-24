/* brake_task.c  – simplified excerpt */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "ecu_hal.h"
#include <stdio.h>
#include <stdint.h>

typedef struct {
    uint16_t wheel_speed[4];   /* rpm  */
    int16_t  ax_mg;            /* longitudinal accel, milli-g */
    uint8_t  pedal_pct;        /* 0..100 */
    uint32_t timestamp_ms;
} sensor_frame_t;

static sensor_frame_t g_frame;          /* written by CAN ISR task, read by control task */
static uint16_t       g_setpoint_mbar;  /* written by control task, read by actuator task */
static QueueHandle_t  q_can;

/* --- CAN receive task, priority 5, runs on every CAN frame --------------- */
static void can_rx_task(void *arg)
{
    can_msg_t msg;
    for (;;) {
        xQueueReceive(q_can, &msg, portMAX_DELAY);
        g_frame.timestamp_ms = xTaskGetTickCount();
        g_frame.pedal_pct    = msg.data[0];
        g_frame.ax_mg        = (int16_t)((msg.data[1] << 8) | msg.data[2]);
        for (int i = 0; i < 4; i++) {
            g_frame.wheel_speed[i] = (msg.data[3 + i] << 8) | msg.data[4 + i];
        }
    }
}

/* --- Control task, priority 4, every 10 ms ------------------------------- */
static uint16_t compute_setpoint(const sensor_frame_t *f)
{
    uint16_t v_max = 0;
    for (int i = 0; i < 4; i++) {
        if (f->wheel_speed[i] > v_max) v_max = f->wheel_speed[i];
    }
    int32_t demand = f->pedal_pct * 100;
    int32_t comp   = (f->ax_mg * v_max) / 1000;
    uint16_t sp    = demand + comp;
    if (sp > 10000) sp = 10000;
    return sp;
}

static void control_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        sensor_frame_t *f = &g_frame;
        if ((xTaskGetTickCount() - f->timestamp_ms) < 100) {
            g_setpoint_mbar = compute_setpoint(f);
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
    }
}

/* --- Actuator task, priority 6, every 5 ms ------------------------------- */
static void actuator_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    char logbuf[32];
    for (;;) {
        uint16_t sp = g_setpoint_mbar;
        pwm_set_duty(sp);
        snprintf(logbuf, sizeof logbuf, "sp=%u t=%lu", sp, xTaskGetTickCount());
        diag_log_async(logbuf);          /* stores the pointer, flushed later by idle hook */
        vTaskDelayUntil(&last, pdMS_TO_TICKS(5));
    }
}

/* --- System start-up ----------------------------------------------------- */
void brake_ecu_start(void)
{
    q_can = xQueueCreate(8, sizeof(can_msg_t));
    xTaskCreate(can_rx_task,   "can_rx",   512, NULL, 5, NULL);
    xTaskCreate(control_task,  "control",  512, NULL, 4, NULL);
    xTaskCreate(actuator_task, "actuator", 512, NULL, 6, NULL);
}
