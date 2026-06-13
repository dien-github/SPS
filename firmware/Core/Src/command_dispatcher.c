#include "command_dispatcher.h"

#include <string.h>

#include "relay_service.h"
#include "projector_service.h"
#include "ir_service.h"
#include "ota_service.h"

extern osMessageQueueId_t relayQueueHandle;
extern osMessageQueueId_t projectorQueueHandle;
extern osMessageQueueId_t irQueueHandle;
extern osMessageQueueId_t otaQueueHandle;

static uint32_t RelayCmd_Pack(uint8_t sourceCmd,
                              uint8_t type,
                              uint8_t deviceId,
                              uint8_t action)
{
    return (((uint32_t)sourceCmd << RELAY_CMD_SOURCE_SHIFT) |
            ((uint32_t)type      << RELAY_CMD_TYPE_SHIFT)   |
            ((uint32_t)deviceId  << RELAY_CMD_DEVICE_SHIFT) |
            ((uint32_t)action    << RELAY_CMD_ACTION_SHIFT));
}

DispatchResult_t Command_DispatchFrame(const SPS_Frame_t *frame)
{
    uint32_t relayCmd;

    if (frame == NULL)
    {
        return DISPATCH_RESULT_INVALID_PARAM;
    }

    switch (frame->cmd_id)
    {
        case SPS_CMD_LIGHT_CONTROL:
            if (frame->length != 2U)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            relayCmd = RelayCmd_Pack(frame->cmd_id,
                                     RELAY_TYPE_LIGHT,
                                     frame->payload[0],
                                     frame->payload[1]);

            if (osMessageQueuePut(relayQueueHandle, &relayCmd, 0U, 0U) != osOK)
            {
                return DISPATCH_RESULT_QUEUE_FULL;
            }

            return DISPATCH_RESULT_OK;

        case SPS_CMD_RELAY_MOTOR:
            if (frame->length != 2U)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            relayCmd = RelayCmd_Pack(frame->cmd_id,
                                     RELAY_TYPE_MOTOR,
                                     frame->payload[0],
                                     frame->payload[1]);

            if (osMessageQueuePut(relayQueueHandle, &relayCmd, 0U, 0U) != osOK)
            {
                return DISPATCH_RESULT_QUEUE_FULL;
            }

            return DISPATCH_RESULT_OK;

        case SPS_CMD_RELAY_STATUS:
            if (frame->length != 1U)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            /*
             * Relay status được xử lý trực tiếp trong commandTask,
             * không đưa vào relayQueue.
             */
            return DISPATCH_RESULT_OK;

        case SPS_CMD_PROJECTOR_CONTROL:
        {
            ProjectorCommand_t projectorCmd;

            if (frame->length != 1U)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            projectorCmd.sourceCmd = frame->cmd_id;
            projectorCmd.action = frame->payload[0];

            if (osMessageQueuePut(projectorQueueHandle, &projectorCmd, 0U, 0U) != osOK)
            {
                return DISPATCH_RESULT_QUEUE_FULL;
            }

            return DISPATCH_RESULT_OK;
        }

        case SPS_CMD_AC_CONTROL:
        {
            IrCommand_t irCmd;

            if (frame->length != 2U)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            irCmd.sourceCmd = frame->cmd_id;
            irCmd.acId = frame->payload[0];
            irCmd.action = frame->payload[1];

            if (osMessageQueuePut(irQueueHandle, &irCmd, 0U, 0U) != osOK)
            {
                return DISPATCH_RESULT_QUEUE_FULL;
            }

            return DISPATCH_RESULT_OK;
        }

        case SPS_CMD_OTA_START:
        {
            OtaCommand_t otaCmd;

            if (frame->length != 4U)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            otaCmd.sourceCmd = frame->cmd_id;
            otaCmd.type = OTA_COMMAND_START;
            otaCmd.firmwareSize = ((uint32_t)frame->payload[0]) |
                                  ((uint32_t)frame->payload[1] << 8) |
                                  ((uint32_t)frame->payload[2] << 16) |
                                  ((uint32_t)frame->payload[3] << 24);
            otaCmd.chunkIndex = 0U;
            otaCmd.dataLength = 0U;

            if (osMessageQueuePut(otaQueueHandle, &otaCmd, 0U, 0U) != osOK)
            {
                return DISPATCH_RESULT_QUEUE_FULL;
            }

            return DISPATCH_RESULT_OK;
        }

        case SPS_CMD_OTA_CHUNK:
        {
            OtaCommand_t otaCmd;
            uint8_t chunkLength;

            if (frame->length < 2U)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            chunkLength = (uint8_t)(frame->length - 1U);

            if (chunkLength > OTA_COMMAND_MAX_DATA_SIZE)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            memset(&otaCmd, 0, sizeof(otaCmd));

            otaCmd.sourceCmd = frame->cmd_id;
            otaCmd.type = OTA_COMMAND_CHUNK;
            otaCmd.chunkIndex = frame->payload[0];
            otaCmd.dataLength = chunkLength;

            memcpy(otaCmd.data, &frame->payload[1], chunkLength);

            if (osMessageQueuePut(otaQueueHandle, &otaCmd, 0U, 0U) != osOK)
            {
                return DISPATCH_RESULT_QUEUE_FULL;
            }

            return DISPATCH_RESULT_OK;
        }

        case SPS_CMD_OTA_END:
        {
            OtaCommand_t otaCmd;

            if (frame->length != 0U)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            memset(&otaCmd, 0, sizeof(otaCmd));

            otaCmd.sourceCmd = frame->cmd_id;
            otaCmd.type = OTA_COMMAND_END;

            if (osMessageQueuePut(otaQueueHandle, &otaCmd, 0U, 0U) != osOK)
            {
                return DISPATCH_RESULT_QUEUE_FULL;
            }

            return DISPATCH_RESULT_OK;
        }

        default:
            return DISPATCH_RESULT_UNKNOWN_CMD;
    }
}
