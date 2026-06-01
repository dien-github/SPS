#ifndef IR_CONTROL_SERVICE_H
#define IR_CONTROL_SERVICE_H

#include "pwm_ir_ll.h"
#include "sps_protocol.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    const ir_pulse_t *pulses;
    size_t count;
} ir_sequence_t;

void ir_control_service_init(void);
bool ir_control_service_submit(const sps_frame_t *frame);
void ir_control_service_task(void *argument);
uint8_t ir_control_service_get_power_state(void);
uint8_t ir_control_service_get_temperature(void);
uint8_t ir_control_service_get_mode(void);

const ir_sequence_t *ir_daikin_get_sequence(uint8_t command_id, uint8_t temperature);

#endif
