#ifndef SPS_PROTOCOL_H
#define SPS_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/*
 * SPS UART Protocol v0.1.0
 *
 * Frame format:
 * [0]      SOF_1   = 0xAA
 * [1]      SOF_2   = 0x55
 * [2]      LENGTH  = payload length only
 * [3]      CMD_ID
 * [4..N]   PAYLOAD
 * [N+1..]  CRC16-CCITT, little-endian: CRC_LOW first, CRC_HIGH second
 *
 * CRC is calculated over:
 * LENGTH + CMD_ID + PAYLOAD
 * Header is not included in CRC.
 */

#define SPS_SOF_1               0xAAU
#define SPS_SOF_2               0x55U

#define SPS_MAX_PAYLOAD_SIZE    255U
#define SPS_MAX_FRAME_SIZE      (2U + 1U + 1U + SPS_MAX_PAYLOAD_SIZE + 2U)

typedef enum
{
    SPS_PARSE_IN_PROGRESS = 0,
    SPS_PARSE_FRAME_READY,
    SPS_PARSE_ERROR
} SPS_ParseResult_t;

typedef enum
{
    SPS_CMD_PING              = 0x10,
    SPS_CMD_ACK               = 0x11,
    SPS_CMD_NACK              = 0x12,

    SPS_CMD_LIGHT_CONTROL     = 0x21,
    SPS_CMD_RELAY_MOTOR       = 0x22,
    SPS_CMD_PROJECTOR_CONTROL = 0x23,
    SPS_CMD_AC_CONTROL        = 0x24,

    SPS_CMD_RELAY_STATUS      = 0x32,

    SPS_CMD_PRESENCE_EVENT    = 0x41,

    SPS_CMD_OTA_START         = 0x50,
    SPS_CMD_OTA_CHUNK         = 0x51,
    SPS_CMD_OTA_END           = 0x52
} SPS_CommandId_t;

typedef enum
{
    SPS_ERR_CRC           = 0x01,
    SPS_ERR_INVALID_PARAM = 0x02,
    SPS_ERR_INVALID_LEN   = 0x03,
    SPS_ERR_UNKNOWN_CMD   = 0x04,
    SPS_ERR_QUEUE_FULL    = 0x05,
    SPS_ERR_TIMEOUT       = 0x06,
    SPS_ERR_OTA           = 0x07
} SPS_ErrorCode_t;

typedef struct
{
    uint8_t length;
    uint8_t cmd_id;
    uint8_t payload[SPS_MAX_PAYLOAD_SIZE];
} SPS_Frame_t;

void SPS_Protocol_Init(void);

SPS_ParseResult_t SPS_Protocol_ParseByte(uint8_t byte, SPS_Frame_t *outFrame);

uint16_t SPS_Protocol_Crc16Ccitt(const uint8_t *data, uint16_t length);

uint16_t SPS_Protocol_BuildFrame(uint8_t cmd_id,
                                 const uint8_t *payload,
                                 uint8_t length,
                                 uint8_t *outBuffer,
                                 uint16_t outBufferSize);

#endif /* SPS_PROTOCOL_H */
