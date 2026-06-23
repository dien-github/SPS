#include "command_queue.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "sps_config.h"

static StaticQueue_t command_queue_control;
static uint8_t command_queue_storage[SPS_COMMAND_QUEUE_DEPTH * sizeof(sps_command_t)];
static QueueHandle_t command_queue_handle;

void command_queue_init(void)
{
    command_queue_handle = xQueueCreateStatic(SPS_COMMAND_QUEUE_DEPTH,
                                              sizeof(sps_command_t),
                                              command_queue_storage,
                                              &command_queue_control);
    configASSERT(command_queue_handle != 0);
}

bool command_queue_push(const sps_frame_t *frame)
{
    sps_command_t command;

    if ((frame == 0) || (command_queue_handle == 0))
    {
        return false;
    }

    command.frame = *frame;
    return xQueueSend(command_queue_handle, &command, 0u) == pdPASS;
}

bool command_queue_pop(sps_command_t *command, uint32_t timeout_ms)
{
    if ((command == 0) || (command_queue_handle == 0))
    {
        return false;
    }

    return xQueueReceive(command_queue_handle, command, pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}
