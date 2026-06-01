#include "projector_service.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "sps_config.h"
#include "sps_types.h"
#include "telemetry_status_manager.h"
#include "timebase.h"
#include "uart_ll.h"
#include <string.h>

static StaticQueue_t projector_queue_control;
static uint8_t projector_queue_storage[SPS_PROJECTOR_QUEUE_DEPTH * sizeof(sps_frame_t)];
static QueueHandle_t projector_queue;
static projector_state_t projector_state;

void projector_service_init(void)
{
    projector_queue = xQueueCreateStatic(SPS_PROJECTOR_QUEUE_DEPTH,
                                         sizeof(sps_frame_t),
                                         projector_queue_storage,
                                         &projector_queue_control);
    configASSERT(projector_queue != 0);
    projector_state = PROJECTOR_STATE_UNKNOWN;
}

bool projector_service_submit(const sps_frame_t *frame)
{
    if ((frame == 0) || (projector_queue == 0))
    {
        return false;
    }

    return xQueueSend(projector_queue, frame, 0u) == pdPASS;
}

uint8_t projector_service_get_state(void)
{
    return (uint8_t)projector_state;
}

static const char *command_to_escvp21(uint8_t command_id, uint32_t *timeout_ms)
{
    *timeout_ms = SPS_PROJECTOR_TIMEOUT_DEFAULT_MS;

    switch (command_id)
    {
        case SPS_PROJECTOR_POWER_ON:
            *timeout_ms = SPS_PROJECTOR_TIMEOUT_PWR_ON_MS;
            return "PWR ON\r";
        case SPS_PROJECTOR_POWER_OFF:
            *timeout_ms = SPS_PROJECTOR_TIMEOUT_PWR_OFF_MS;
            return "PWR OFF\r";
        case SPS_PROJECTOR_GET_POWER_STATUS:
            return "PWR?\r";
        case SPS_PROJECTOR_SOURCE_HDMI:
            *timeout_ms = SPS_PROJECTOR_TIMEOUT_SOURCE_MS;
            return "SOURCE 30\r";
        case SPS_PROJECTOR_SOURCE_VGA:
            *timeout_ms = SPS_PROJECTOR_TIMEOUT_SOURCE_MS;
            return "SOURCE 11\r";
        case SPS_PROJECTOR_MUTE_ON:
            return "MUTE ON\r";
        case SPS_PROJECTOR_MUTE_OFF:
            return "MUTE OFF\r";
        case SPS_PROJECTOR_GET_ERROR_STATUS:
            return "ERR?\r";
        default:
            return 0;
    }
}

static sps_error_code_t wait_projector_response(uint32_t timeout_ms, uint8_t *response_code)
{
    char text[24];
    uint8_t pos = 0u;
    uint32_t start = timebase_millis();

    while ((uint32_t)(timebase_millis() - start) < timeout_ms)
    {
        uint8_t byte;

        while (uart_ll_read_byte(UART_LL_PORT_PROJECTOR, &byte))
        {
            if (byte == ':')
            {
                *response_code = 0u;
                return SPS_ERR_NONE;
            }

            if ((byte == '\r') || (byte == '\n'))
            {
                text[pos] = '\0';
                if ((pos >= 3u) && (text[0] == 'E') && (text[1] == 'R') && (text[2] == 'R'))
                {
                    *response_code = 1u;
                    return SPS_ERR_PROJECTOR_ERR;
                }
                pos = 0u;
            }
            else if (pos < (sizeof(text) - 1u))
            {
                text[pos++] = (char)byte;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5u));
    }

    *response_code = 0xFFu;
    return SPS_ERR_TIMEOUT;
}

static void execute_projector_frame(const sps_frame_t *frame)
{
    const char *cmd;
    uint32_t timeout_ms;
    uint8_t data[2] = { 0u, 0u };
    sps_error_code_t error;

    cmd = command_to_escvp21(frame->command_id, &timeout_ms);
    if (cmd == 0)
    {
        (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_FAIL, SPS_ERR_UNKNOWN_COMMAND, 0, 0u);
        return;
    }

    projector_state = PROJECTOR_STATE_BUSY;
    telemetry_status_manager_set_projector_state((uint8_t)projector_state);

    if (!uart_ll_write(UART_LL_PORT_PROJECTOR, (const uint8_t *)cmd, strlen(cmd), 250u))
    {
        projector_state = PROJECTOR_STATE_ERROR;
        (void)telemetry_send_response(frame->seq, frame->device_id, frame->command_id, SPS_STATUS_FAIL, SPS_ERR_TIMEOUT, 0, 0u);
        return;
    }

    error = wait_projector_response(timeout_ms, &data[0]);
    data[1] = SPS_STATE_SOURCE_ASSUMED;
    projector_state = (error == SPS_ERR_NONE) ? PROJECTOR_STATE_READY : PROJECTOR_STATE_ERROR;
    telemetry_status_manager_set_projector_state((uint8_t)projector_state);

    (void)telemetry_send_response(frame->seq,
                                  frame->device_id,
                                  frame->command_id,
                                  (error == SPS_ERR_NONE) ? SPS_STATUS_OK : SPS_STATUS_FAIL,
                                  error,
                                  data,
                                  sizeof(data));
}

void projector_service_task(void *argument)
{
    sps_frame_t frame;

    (void)argument;
    for (;;)
    {
        if (xQueueReceive(projector_queue, &frame, portMAX_DELAY) == pdTRUE)
        {
            execute_projector_frame(&frame);
        }
    }
}
