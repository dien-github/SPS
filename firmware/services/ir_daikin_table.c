#include "ir_control_service.h"
#include "sps_protocol.h"

static const ir_pulse_t demo_ac_on[] =
{
    { 9000u, 4500u }, { 560u, 560u }, { 560u, 1690u }, { 560u, 560u },
    { 560u, 1690u }, { 560u, 560u }, { 560u, 1690u }, { 560u, 20000u }
};

static const ir_pulse_t demo_ac_off[] =
{
    { 9000u, 4500u }, { 560u, 1690u }, { 560u, 560u }, { 560u, 1690u },
    { 560u, 560u }, { 560u, 1690u }, { 560u, 560u }, { 560u, 20000u }
};

static const ir_pulse_t demo_mode[] =
{
    { 4500u, 4500u }, { 560u, 560u }, { 560u, 560u }, { 560u, 1690u },
    { 560u, 1690u }, { 560u, 560u }, { 560u, 20000u }
};

const ir_sequence_t *ir_daikin_get_sequence(uint8_t command_id, uint8_t temperature)
{
    static const ir_sequence_t ac_on = { demo_ac_on, sizeof(demo_ac_on) / sizeof(demo_ac_on[0]) };
    static const ir_sequence_t ac_off = { demo_ac_off, sizeof(demo_ac_off) / sizeof(demo_ac_off[0]) };
    static const ir_sequence_t mode = { demo_mode, sizeof(demo_mode) / sizeof(demo_mode[0]) };

    (void)temperature;

    switch (command_id)
    {
        case SPS_AC_ON:
        case SPS_AC_SET_TEMP:
            return &ac_on;
        case SPS_AC_OFF:
            return &ac_off;
        case SPS_AC_COOL_MODE:
        case SPS_AC_FAN_MODE:
        case SPS_AC_DRY_MODE:
            return &mode;
        default:
            return 0;
    }
}
