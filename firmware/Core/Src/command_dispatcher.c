#include "command_dispatcher.h"

#include "relay_service.h"
#include "projector_service.h"
#include "ir_service.h"
#include "ota_service.h"

extern osMessageQueueId_t relayQueueHandle;

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

static DispatchResult_t Dispatch_FromOtaResult(OtaResult_t otaResult)
{
    if (otaResult == OTA_RESULT_OK)
    {
        return DISPATCH_RESULT_OK;
    }

    if (otaResult == OTA_RESULT_INVALID_PARAM)
    {
        return DISPATCH_RESULT_INVALID_PARAM;
    }

    return DISPATCH_RESULT_OTA_ERROR;
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
            ProjectorResult_t projectorResult;

            if (frame->length != 1U)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            projectorResult = Projector_Service_Control(frame->payload[0]);

            if (projectorResult == PROJECTOR_RESULT_INVALID_PARAM)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            if (projectorResult == PROJECTOR_RESULT_UART_ERROR)
            {
                return DISPATCH_RESULT_PROJECTOR_ERROR;
            }

            return DISPATCH_RESULT_OK;
        }

        case SPS_CMD_AC_CONTROL:
        {
            IrResult_t irResult;

            if (frame->length != 2U)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            irResult = IR_Service_ControlAC(frame->payload[0],
                                            frame->payload[1]);

            if (irResult == IR_RESULT_INVALID_PARAM)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            return DISPATCH_RESULT_OK;
        }

        case SPS_CMD_OTA_START:
        {
            uint32_t firmwareSize;
            OtaResult_t otaResult;

            if (frame->length != 4U)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            firmwareSize = ((uint32_t)frame->payload[0]) |
                           ((uint32_t)frame->payload[1] << 8) |
                           ((uint32_t)frame->payload[2] << 16) |
                           ((uint32_t)frame->payload[3] << 24);

            otaResult = OTA_Service_Start(firmwareSize);

            return Dispatch_FromOtaResult(otaResult);
        }

        case SPS_CMD_OTA_CHUNK:
        {
            uint8_t chunkIndex;
            const uint8_t *chunkData;
            uint8_t chunkLength;
            OtaResult_t otaResult;

            if (frame->length < 2U)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            chunkIndex = frame->payload[0];
            chunkData = &frame->payload[1];
            chunkLength = (uint8_t)(frame->length - 1U);

            otaResult = OTA_Service_WriteChunk(chunkIndex,
                                               chunkData,
                                               chunkLength);

            return Dispatch_FromOtaResult(otaResult);
        }

        case SPS_CMD_OTA_END:
        {
            OtaResult_t otaResult;

            if (frame->length != 0U)
            {
                return DISPATCH_RESULT_INVALID_PARAM;
            }

            otaResult = OTA_Service_End();

            return Dispatch_FromOtaResult(otaResult);
        }

        default:
            return DISPATCH_RESULT_UNKNOWN_CMD;
    }
}
