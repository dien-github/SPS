#include "ir_control_service.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "sps_config.h"
#include "sps_error.h"
#include "sps_types.h"
#include "telemetry_status_manager.h"

static StaticQueue_t ir_queue_control;
static uint8_t ir_queue_storage[SPS_IR_QUEUE_DEPTH * sizeof(sps_frame_t)];
static QueueHandle_t ir_queue;
static volatile bool ir_busy;
static uint8_t ac_power;
static uint8_t ac_temperature;
static uint8_t ac_mode;

void ir_control_service_init(void)
{
    pwm_ir_ll_init();
    ir_queue = xQueueCreateStatic(SPS_IR_QUEUE_DEPTH, sizeof(sps_frame_t), ir_queue_storage, &ir_queue_control);
    configASSERT(ir_queue != 0);
    ir_busy = false;
    ac_power = 0u;
    ac_temperature = 24u;
    ac_mode = SPS_AC_COOL_MODE;
}

bool ir_control_service_submit(const sps_frame_t *frame)
{
    if ((frame == 0) || (ir_queue == 0) || ir_busy)
    {
        return false;
    }

    return xQueueSend(ir_queue, frame, 0u) == pdPASS;
}

uint8_t ir_control_service_get_power_state(void)
{
    return ac_power;
}

uint8_t ir_control_service_get_temperature(void)
{
    return ac_temperature;
}

uint8_t ir_control_service_get_mode(void)
{
    return ac_mode;
}

static void update_assumed_state(const sps_frame_t *frame)
{
    switch (frame->command_id)
    {
        case SPS_AC_ON:
            ac_power = 1u;
            break;
        case SPS_AC_OFF:
            ac_power = 0u;
            break;
        case SPS_AC_SET_TEMP:
            if ((frame->payload_len == 1u) && (frame->payload[0] >= 16u) && (frame->payload[0] <= 30u))
            {
                ac_temperature = frame->payload[0];
            }
            break;
        case SPS_AC_COOL_MODE:
        case SPS_AC_FAN_MODE:
        case SPS_AC_DRY_MODE:
            ac_mode = frame->command_id;
            break;
        default:
            break;
    }
}

void ir_control_service_task(void *argument)
{
    sps_frame_t frame;

    (void)argument;

    for (;;)
    {
        if (xQueueReceive(ir_queue, &frame, portMAX_DELAY) == pdTRUE)
        {
            const ir_sequence_t *sequence;
            uint8_t data[4];
            sps_error_code_t error = SPS_ERR_NONE;

            if (frame.command_id == SPS_AC_GET_ASSUMED_STATE)
            {
                data[0] = ac_power;
                data[1] = ac_temperature;
                data[2] = ac_mode;
                data[3] = SPS_STATE_SOURCE_ASSUMED;
                (void)telemetry_send_response(frame.seq, frame.device_id, frame.command_id, SPS_STATUS_OK, SPS_ERR_NONE, data, sizeof(data));
                continue;
            }

            if ((frame.command_id == SPS_AC_SET_TEMP) &&
                ((frame.payload_len != 1u) || (frame.payload[0] < 16u) || (frame.payload[0] > 30u)))
            {
                (void)telemetry_send_response(frame.seq, frame.device_id, frame.command_id, SPS_STATUS_FAIL, SPS_ERR_INVALID_PAYLOAD, 0, 0u);
                continue;
            }

            sequence = ir_daikin_get_sequence(frame.command_id, (frame.payload_len == 1u) ? frame.payload[0] : ac_temperature);
            if (sequence == 0)
            {
                (void)telemetry_send_response(frame.seq, frame.device_id, frame.command_id, SPS_STATUS_FAIL, SPS_ERR_UNKNOWN_COMMAND, 0, 0u);
                continue;
            }

            ir_busy = true;
            if (!pwm_ir_ll_send_raw_blocking(sequence->pulses, sequence->count))
            {
                error = SPS_ERR_IR_BUSY;
            }
            ir_busy = false;

            if (error == SPS_ERR_NONE)
            {
                update_assumed_state(&frame);
            }

            data[0] = ac_power;
            data[1] = ac_temperature;
            data[2] = ac_mode;
            data[3] = SPS_STATE_SOURCE_ASSUMED;
            (void)telemetry_send_response(frame.seq,
                                          frame.device_id,
                                          frame.command_id,
                                          (error == SPS_ERR_NONE) ? SPS_STATUS_ASSUMED_SUCCESS_OPEN_LOOP : SPS_STATUS_FAIL,
                                          error,
                                          data,
                                          sizeof(data));
        }
    }
}
