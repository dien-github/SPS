#ifndef PWM_IR_LL_H
#define PWM_IR_LL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint16_t mark_us;
    uint16_t space_us;
} ir_pulse_t;

void pwm_ir_ll_init(void);
void pwm_ir_ll_carrier_on(void);
void pwm_ir_ll_carrier_off(void);
bool pwm_ir_ll_send_raw_blocking(const ir_pulse_t *pulses, size_t count);

#endif
