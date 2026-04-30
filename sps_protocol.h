#ifndef SPS_PROTOCOL_H
#define SPS_PROTOCOL_H

#include <stdint.h>

#define SPS_SOF_1                  0xAAU
#define SPS_SOF_2                  0x55U
#define SPS_MAX_PAYLOAD_SIZE       255U

#define SPS_CMD_PING               0x10U
#define SPS_CMD_ACK                0x11U
#define SPS_CMD_NACK               0x12U

#define SPS_CMD_LIGHT_CONTROL      0x21U
#define SPS_CMD_RELAY_CONTROL      0x22U
#define SPS_CMD_RELAY_STATUS       0x32U

#define SPS_ERR_CRC                0x01U
#define SPS_ERR_INVALID_PARAM      0x02U
#define SPS_ERR_UNKNOWN_CMD        0x03U
#define SPS_ERR_QUEUE_FULL         0x04U
#define SPS_ERR_BAD_LENGTH         0x05U

typedef struct
{
    uint8_t length;
    uint8_t cmd_id;
    uint8_t payload[SPS_MAX_PAYLOAD_SIZE];
} sps_frame_t;

typedef enum
{
    SPS_PARSE_WAIT_SOF1 = 0,
    SPS_PARSE_WAIT_SOF2,
    SPS_PARSE_LENGTH,
    SPS_PARSE_CMD,
    SPS_PARSE_PAYLOAD,
    SPS_PARSE_CRC_HIGH,
    SPS_PARSE_CRC_LOW
} sps_parse_state_t;

typedef struct
{
    sps_parse_state_t state;

    uint8_t length;
    uint8_t cmd_id;
    uint8_t payload[SPS_MAX_PAYLOAD_SIZE];
    uint8_t payload_index;

    uint16_t crc_calc;
    uint16_t crc_rx;
} sps_parser_t;

void SPS_Protocol_ParserInit(sps_parser_t *parser);
int32_t SPS_Protocol_ParserPushByte(sps_parser_t *parser, uint8_t byte, sps_frame_t *out_frame);

void SPS_Protocol_SendFrame(uint8_t cmd_id, const uint8_t *payload, uint8_t length);
void SPS_Protocol_SendAck(uint8_t ack_cmd_id);
void SPS_Protocol_SendNack(uint8_t failed_cmd_id, uint8_t error_code);

#endif
