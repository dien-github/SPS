#include "ota_service.h"

#include <stddef.h>

/*
 * OTA skeleton.
 *
 * Hiện tại module này chưa ghi Flash thật.
 * Nhiệm vụ hiện tại:
 * - Kiểm tra đúng thứ tự OTA_START -> OTA_CHUNK -> OTA_END
 * - Kiểm tra firmware size
 * - Kiểm tra chunk index tăng tuần tự
 * - Đếm số byte đã nhận
 *
 * Giai đoạn sau mới mở rộng:
 * - Erase Flash OTA slot
 * - Write Flash
 * - Verify CRC32/Image
 * - Set boot flag
 * - Reboot sang firmware mới
 */

static uint8_t otaInProgress;
static uint32_t otaExpectedSize;
static uint32_t otaReceivedSize;
static uint8_t otaExpectedChunkIndex;

void OTA_Service_Init(void)
{
    otaInProgress = 0U;
    otaExpectedSize = 0U;
    otaReceivedSize = 0U;
    otaExpectedChunkIndex = 0U;
}

OtaResult_t OTA_Service_Start(uint32_t firmwareSize)
{
    if (firmwareSize == 0U)
    {
        return OTA_RESULT_INVALID_PARAM;
    }

    otaInProgress = 1U;
    otaExpectedSize = firmwareSize;
    otaReceivedSize = 0U;
    otaExpectedChunkIndex = 0U;

    return OTA_RESULT_OK;
}

OtaResult_t OTA_Service_WriteChunk(uint8_t chunkIndex,
                                   const uint8_t *data,
                                   uint8_t dataLength)
{
    if (otaInProgress == 0U)
    {
        return OTA_RESULT_SEQUENCE_ERROR;
    }

    if (data == NULL)
    {
        return OTA_RESULT_INVALID_PARAM;
    }

    if (dataLength == 0U)
    {
        return OTA_RESULT_INVALID_PARAM;
    }

    if (chunkIndex != otaExpectedChunkIndex)
    {
        return OTA_RESULT_SEQUENCE_ERROR;
    }

    if ((otaReceivedSize + dataLength) > otaExpectedSize)
    {
        otaInProgress = 0U;
        return OTA_RESULT_INVALID_PARAM;
    }

    /*
     * Skeleton:
     * Sau này ghi data vào Flash tại đây.
     */
    otaReceivedSize += dataLength;
    otaExpectedChunkIndex++;

    return OTA_RESULT_OK;
}

OtaResult_t OTA_Service_End(void)
{
    if (otaInProgress == 0U)
    {
        return OTA_RESULT_SEQUENCE_ERROR;
    }

    if (otaReceivedSize != otaExpectedSize)
    {
        otaInProgress = 0U;
        return OTA_RESULT_VERIFY_FAIL;
    }

    otaInProgress = 0U;

    /*
     * Skeleton:
     * Sau này verify CRC/Image và reset MCU tại đây.
     */
    return OTA_RESULT_OK;
}

uint8_t OTA_Service_IsInProgress(void)
{
    return otaInProgress;
}

uint32_t OTA_Service_GetExpectedSize(void)
{
    return otaExpectedSize;
}

uint32_t OTA_Service_GetReceivedSize(void)
{
    return otaReceivedSize;
}

OtaResult_t OTA_Service_ExecuteCommand(const OtaCommand_t *cmd)
{
    if (cmd == NULL)
    {
        return OTA_RESULT_INVALID_PARAM;
    }

    switch (cmd->type)
    {
        case OTA_COMMAND_START:
            return OTA_Service_Start(cmd->firmwareSize);

        case OTA_COMMAND_CHUNK:
            return OTA_Service_WriteChunk(cmd->chunkIndex,
                                          cmd->data,
                                          cmd->dataLength);

        case OTA_COMMAND_END:
            return OTA_Service_End();

        default:
            return OTA_RESULT_INVALID_PARAM;
    }
}
