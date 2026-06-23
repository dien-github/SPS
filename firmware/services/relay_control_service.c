#include "relay_control_service.h"
#include "gpio_relay_ll.h"
#include "sps_board_config.h"
#include "sps_config.h"
#include "sps_types.h"
#include "telemetry_status_manager.h"
#include "timebase.h"
#include <stdbool.h>

typedef struct
{
    uint8_t relay_forward;
    uint8_t relay_reverse;
    relay_axis_state_t state;
    relay_axis_state_t pending;
    uint32_t deadtime_until_ms;
    uint32_t active_until_ms;
    uint32_t move_timeout_ms;
} relay_axis_t;

static bool light_on;
static relay_axis_t curtain;
static relay_axis_t screen;

static void axis_off(relay_axis_t *axis)
{
    (void)gpio_relay_ll_set(axis->relay_forward, false);
    (void)gpio_relay_ll_set(axis->relay_reverse, false);
    axis->state = RELAY_AXIS_STOPPED;
    axis->pending = RELAY_AXIS_STOPPED;
    axis->active_until_ms = 0u;
    axis->deadtime_until_ms = 0u;
}

static sps_error_code_t axis_start(relay_axis_t *axis, relay_axis_state_t direction)
{
    uint32_t now = timebase_millis();

    if ((direction != RELAY_AXIS_FORWARD) && (direction != RELAY_AXIS_REVERSE))
    {
        return SPS_ERR_INVALID_PAYLOAD;
    }

    if (axis->state != RELAY_AXIS_STOPPED)
    {
        if (axis->state == direction)
        {
            axis->active_until_ms = now + axis->move_timeout_ms;
            return SPS_ERR_NONE;
        }

        (void)gpio_relay_ll_set(axis->relay_forward, false);
        (void)gpio_relay_ll_set(axis->relay_reverse, false);
        axis->state = RELAY_AXIS_STOPPED;
        axis->pending = direction;
        axis->deadtime_until_ms = now + SPS_RELAY_DEADTIME_MS;
        return SPS_ERR_NONE;
    }

    if ((axis->pending != RELAY_AXIS_STOPPED) && ((int32_t)(now - axis->deadtime_until_ms) < 0))
    {
        return SPS_ERR_RELAY_INTERLOCK;
    }

    if (direction == RELAY_AXIS_FORWARD)
    {
        (void)gpio_relay_ll_set(axis->relay_reverse, false);
        (void)gpio_relay_ll_set(axis->relay_forward, true);
    }
    else
    {
        (void)gpio_relay_ll_set(axis->relay_forward, false);
        (void)gpio_relay_ll_set(axis->relay_reverse, true);
    }

    axis->state = direction;
    axis->active_until_ms = now + axis->move_timeout_ms;
    axis->pending = RELAY_AXIS_STOPPED;
    return SPS_ERR_NONE;
}

void relay_control_service_init(void)
{
    gpio_relay_ll_init();
    gpio_relay_ll_all_off();
    light_on = false;
    curtain.relay_forward = SPS_RELAY_CURTAIN_OPEN;
    curtain.relay_reverse = SPS_RELAY_CURTAIN_CLOSE;
    curtain.move_timeout_ms = SPS_CURTAIN_MOVE_TIMEOUT_MS;
    screen.relay_forward = SPS_RELAY_SCREEN_UP;
    screen.relay_reverse = SPS_RELAY_SCREEN_DOWN;
    screen.move_timeout_ms = SPS_SCREEN_MOVE_TIMEOUT_MS;
    axis_off(&curtain);
    axis_off(&screen);
}

static sps_error_code_t handle_light(const sps_frame_t *frame)
{
    switch (frame->command_id)
    {
        case SPS_LIGHT_ON:
            light_on = true;
            (void)gpio_relay_ll_set(SPS_RELAY_LIGHT_MAIN, true);
            break;
        case SPS_LIGHT_OFF:
            light_on = false;
            (void)gpio_relay_ll_set(SPS_RELAY_LIGHT_MAIN, false);
            break;
        case SPS_LIGHT_TOGGLE:
            light_on = !light_on;
            (void)gpio_relay_ll_set(SPS_RELAY_LIGHT_MAIN, light_on);
            break;
        case SPS_LIGHT_GET_STATE:
            break;
        default:
            return SPS_ERR_UNKNOWN_COMMAND;
    }
    return SPS_ERR_NONE;
}

static sps_error_code_t handle_axis(const sps_frame_t *frame, relay_axis_t *axis, uint8_t open_cmd, uint8_t close_cmd, uint8_t stop_cmd, uint8_t get_cmd)
{
    if (frame->command_id == open_cmd)
    {
        return axis_start(axis, RELAY_AXIS_FORWARD);
    }
    if (frame->command_id == close_cmd)
    {
        return axis_start(axis, RELAY_AXIS_REVERSE);
    }
    if (frame->command_id == stop_cmd)
    {
        axis_off(axis);
        return SPS_ERR_NONE;
    }
    if (frame->command_id == get_cmd)
    {
        return SPS_ERR_NONE;
    }
    return SPS_ERR_UNKNOWN_COMMAND;
}

sps_error_code_t relay_control_service_handle_command(const sps_frame_t *frame)
{
    sps_error_code_t error;
    uint8_t data[2];

    if (frame == 0)
    {
        return SPS_ERR_INVALID_PAYLOAD;
    }

    switch (frame->device_id)
    {
        case SPS_DEV_LIGHT:
            error = handle_light(frame);
            data[0] = relay_control_service_get_light_state();
            data[1] = SPS_STATE_SOURCE_ASSUMED;
            break;
        case SPS_DEV_CURTAIN:
            error = handle_axis(frame, &curtain, SPS_CURTAIN_OPEN, SPS_CURTAIN_CLOSE, SPS_CURTAIN_STOP, SPS_CURTAIN_GET_STATE);
            data[0] = relay_control_service_get_curtain_state();
            data[1] = SPS_STATE_SOURCE_ASSUMED;
            break;
        case SPS_DEV_SCREEN:
            error = handle_axis(frame, &screen, SPS_SCREEN_UP, SPS_SCREEN_DOWN, SPS_SCREEN_STOP, SPS_SCREEN_GET_STATE);
            data[0] = relay_control_service_get_screen_state();
            data[1] = SPS_STATE_SOURCE_ASSUMED;
            break;
        case SPS_DEV_RELAY_RAW:
            if (frame->command_id == SPS_RELAY_RAW_ALL_OFF)
            {
                gpio_relay_ll_all_off();
                light_on = false;
                axis_off(&curtain);
                axis_off(&screen);
                data[0] = 0u;
                data[1] = SPS_STATE_SOURCE_ASSUMED;
                error = SPS_ERR_NONE;
            }
            else if ((frame->command_id == SPS_RELAY_RAW_SET_CHANNEL) && (frame->payload_len == 2u))
            {
                error = gpio_relay_ll_set(frame->payload[0], frame->payload[1] != 0u) ? SPS_ERR_NONE : SPS_ERR_INVALID_PAYLOAD;
                data[0] = (uint8_t)gpio_relay_ll_get(frame->payload[0]);
                data[1] = SPS_STATE_SOURCE_ASSUMED;
            }
            else if ((frame->command_id == SPS_RELAY_RAW_GET_CHANNEL) && (frame->payload_len == 1u))
            {
                data[0] = (uint8_t)gpio_relay_ll_get(frame->payload[0]);
                data[1] = SPS_STATE_SOURCE_ASSUMED;
                error = SPS_ERR_NONE;
            }
            else
            {
                error = SPS_ERR_UNKNOWN_COMMAND;
                data[0] = 0u;
                data[1] = SPS_STATE_SOURCE_ASSUMED;
            }
            break;
        default:
            return SPS_ERR_UNKNOWN_DEVICE;
    }

    (void)telemetry_send_response(frame->seq,
                                  frame->device_id,
                                  frame->command_id,
                                  (error == SPS_ERR_NONE) ? SPS_STATUS_OK : SPS_STATUS_FAIL,
                                  error,
                                  data,
                                  sizeof(data));
    return error;
}

void relay_control_service_periodic(void)
{
    relay_axis_t *axes[] = { &curtain, &screen };
    uint32_t now = timebase_millis();

    for (uint8_t i = 0u; i < 2u; i++)
    {
        relay_axis_t *axis = axes[i];

        if ((axis->pending != RELAY_AXIS_STOPPED) && ((int32_t)(now - axis->deadtime_until_ms) >= 0))
        {
            relay_axis_state_t pending = axis->pending;
            axis->pending = RELAY_AXIS_STOPPED;
            (void)axis_start(axis, pending);
        }

        if ((axis->state != RELAY_AXIS_STOPPED) && (axis->active_until_ms != 0u) && ((int32_t)(now - axis->active_until_ms) >= 0))
        {
            axis_off(axis);
        }
    }
}

uint8_t relay_control_service_get_light_state(void)
{
    return light_on ? 1u : 0u;
}

uint8_t relay_control_service_get_curtain_state(void)
{
    return (uint8_t)curtain.state;
}

uint8_t relay_control_service_get_screen_state(void)
{
    return (uint8_t)screen.state;
}
