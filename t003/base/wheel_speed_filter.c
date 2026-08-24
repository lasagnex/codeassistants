/* wheel_speed_filter.c */
#include "wheel_speed_filter.h"

#define WSF_WINDOW 8u

static uint16_t samples[WSF_WINDOW];
static uint8_t  index;
static uint32_t sum;

void wsf_init(void)
{
    index = 0;
    sum   = 0;
}

/* speed_rpm: raw wheel speed in rpm, 0 .. 3000 */
uint16_t wsf_update(uint16_t speed_rpm)
{
    sum -= samples[index];
    samples[index] = speed_rpm;
    sum += speed_rpm;

    index++;
    if (index > WSF_WINDOW) {
        index = 0;
    }

    return (uint16_t)(sum / WSF_WINDOW);
}
