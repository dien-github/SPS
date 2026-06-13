#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <stdint.h>

typedef enum
{
    OTA_RESULT_OK = 0,
    OTA_RESULT_INVALID_PARAM,
    OTA_RESULT_SEQUENCE_ERROR,
    OTA_RESULT_VERIFY_FAIL
} OtaResult_t;

typedef enum
{
    OTA_COMMAND_START = 0,
    OTA_COMMAND_CHUNK,
    OTA_COMMAND_END
} OtaCommandType_t;

#define OTA_COMMAND_MAX_DATA_SIZE  64U

typedef struct
{
    uint8_t sourceCmd;
    OtaCommandType_t type;

    uint32_t firmwareSize;

    uint8_t chunkIndex;
    uint8_t dataLength;
    uint8_t data[OTA_COMMAND_MAX_DATA_SIZE];
} OtaCommand_t;

void OTA_Service_Init(void);

OtaResult_t OTA_Service_ExecuteCommand(const OtaCommand_t *cmd);

OtaResult_t OTA_Service_Start(uint32_t firmwareSize);

OtaResult_t OTA_Service_WriteChunk(uint8_t chunkIndex,
                                   const uint8_t *data,
                                   uint8_t dataLength);

OtaResult_t OTA_Service_End(void);

uint8_t OTA_Service_IsInProgress(void);
uint32_t OTA_Service_GetExpectedSize(void);
uint32_t OTA_Service_GetReceivedSize(void);

#endif /* OTA_SERVICE_H */
