#ifndef RELAY_CONTROL_SERVICE_H
#define RELAY_CONTROL_SERVICE_H

#include "sps_error.h"
#include "sps_protocol.h"
#include <stdint.h>

typedef enum
{
    RELAY_AXIS_STOPPED = 0,
    RELAY_AXIS_FORWARD = 1,
    RELAY_AXIS_REVERSE = 2
} relay_axis_state_t;

void relay_control_service_init(void);
sps_error_code_t relay_control_service_handle_command(const sps_frame_t *frame);
void relay_control_service_periodic(void);
uint8_t relay_control_service_get_light_state(void);
uint8_t relay_control_service_get_curtain_state(void);
uint8_t relay_control_service_get_screen_state(void);

#endif
