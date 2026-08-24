/* wheel_speed_filter.h */
#ifndef WHEEL_SPEED_FILTER_H
#define WHEEL_SPEED_FILTER_H
#include <stdint.h>

void     wsf_init(void);
uint16_t wsf_update(uint16_t speed_rpm);

#endif
