#include "command_dispatcher.h"
#include "command_queue.h"
#include "ir_control_service.h"
#include "ota_update_manager.h"
#include "projector_service.h"
#include "relay_control_service.h"
#include "sps_config.h"
#include "sps_error.h"
#include "telemetry_status_manager.h"
#include "stm32f4xx.h"

static void send_version(const sps_frame_t *frame)
{
    uint8_t data[4];
    uint32_t version = SPS_FIRMWARE_VERSION_U32;

    data[0] = (uint8_t)(version & 0xFFu);
    data[1] = (uint8_t)(version >> 8);
    data[2] = (uint8_t)(version >> 16);
    data[3] = (uint8_t)(version >> 24);
    (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_OK, SPS_ERR_NONE, data, sizeof(data));
}

static void handle_system(const sps_frame_t *frame)
{
    if (frame->command_id == SPS_SYS_GET_VERSION)
    {
        send_version(frame);
    }
    else if ((frame->command_id == SPS_SYS_GET_STATUS) || (frame->command_id == SPS_SYS_PING))
    {
        (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_OK, SPS_ERR_NONE, 0, 0u);
    }
    else if (frame->command_id == SPS_SYS_RESET_MCU)
    {
        (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_ACCEPTED, SPS_ERR_NONE, 0, 0u);
        for (volatile uint32_t i = 0u; i < 100000u; i++)
        {
            __asm volatile ("nop");
        }
        NVIC_SystemReset();
    }
    else if (frame->command_id == SPS_SYS_SET_TIME_OPTIONAL)
    {
        (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_UNSUPPORTED, SPS_ERR_UNSUPPORTED_ON_MCU, 0, 0u);
    }
    else
    {
        (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_FAIL, SPS_ERR_UNKNOWN_COMMAND, 0, 0u);
    }
}

static void dispatch_frame(const sps_frame_t *frame)
{
    if (frame->msg_type == SPS_MSG_PING)
    {
        (void)telemetry_send_pong(frame->seq);
        return;
    }

    if ((frame->msg_type == SPS_MSG_OTA_START) ||
        (frame->msg_type == SPS_MSG_OTA_BLOCK) ||
        (frame->msg_type == SPS_MSG_OTA_END) ||
        (frame->msg_type == SPS_MSG_OTA_ABORT))
    {
        if (!ota_update_manager_submit(frame))
        {
            (void)telemetry_send_response(frame->seq, SPS_DEV_OTA, frame->command_id, SPS_STATUS_BUSY, SPS_ERR_QUEUE_FULL, 0, 0u);
        }
        return;
    }

    if (frame->msg_type != SPS_MSG_COMMAND)
    {
        (void)telemetry_send_error(frame->seq, frame->device_id, frame->command_id, SPS_ERR_INVALID_PAYLOAD);
        return;
    }

    switch (frame->device_id)
    {
        case SPS_DEV_SYSTEM:
            handle_system(frame);
            break;

        case SPS_DEV_PROJECTOR:
            if (!projector_service_submit(frame))
            {
                (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_BUSY, SPS_ERR_QUEUE_FULL, 0, 0u);
            }
            else
            {
                (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_ACCEPTED, SPS_ERR_NONE, 0, 0u);
            }
            break;

        case SPS_DEV_LIGHT:
        case SPS_DEV_CURTAIN:
        case SPS_DEV_SCREEN:
        case SPS_DEV_RELAY_RAW:
            (void)relay_control_service_handle_command(frame);
            break;

        case SPS_DEV_AC_IR:
            if (!ir_control_service_submit(frame))
            {
                (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_BUSY, SPS_ERR_IR_BUSY, 0, 0u);
            }
            else
            {
                (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_ACCEPTED, SPS_ERR_NONE, 0, 0u);
            }
            break;

        case SPS_DEV_OTA:
            if (!ota_update_manager_submit(frame))
            {
                (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_BUSY, SPS_ERR_QUEUE_FULL, 0, 0u);
            }
            break;

        case SPS_DEV_COMPUTER_WOL_UNSUPPORTED_ON_MCU:
            (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_UNSUPPORTED, SPS_ERR_UNSUPPORTED_ON_MCU, 0, 0u);
            break;

        default:
            (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_FAIL, SPS_ERR_UNKNOWN_DEVICE, 0, 0u);
            break;
    }
}

void command_dispatcher_task(void *argument)
{
    sps_command_t command;

    (void)argument;

    for (;;)
    {
        if (command_queue_pop(&command, 1000u))
        {
            dispatch_frame(&command.frame);
        }
    }
}
