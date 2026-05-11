#include "sps_protocol.h"

#include <string.h>

typedef enum
{
    PARSER_WAIT_SOF_1 = 0,
    PARSER_WAIT_SOF_2,
    PARSER_WAIT_LENGTH,
    PARSER_WAIT_CMD,
    PARSER_WAIT_PAYLOAD,
    PARSER_WAIT_CRC_LOW,
    PARSER_WAIT_CRC_HIGH
} ParserState_t;

static ParserState_t parserState;
static SPS_Frame_t currentFrame;
static uint16_t payloadIndex;
static uint16_t receivedCrc;

static uint16_t SPS_Protocol_CalcFrameCrc(const SPS_Frame_t *frame);

void SPS_Protocol_Init(void)
{
    parserState = PARSER_WAIT_SOF_1;
    payloadIndex = 0U;
    receivedCrc = 0U;
    memset(&currentFrame, 0, sizeof(currentFrame));
}

uint16_t SPS_Protocol_Crc16Ccitt(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;

    if ((data == NULL) && (length > 0U))
    {
        return 0U;
    }

    for (uint16_t i = 0U; i < length; i++)
    {
        crc ^= ((uint16_t)data[i] << 8);

        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

static uint16_t SPS_Protocol_CalcFrameCrc(const SPS_Frame_t *frame)
{
    uint8_t crcBuffer[2U + SPS_MAX_PAYLOAD_SIZE];

    if (frame == NULL)
    {
        return 0U;
    }

    crcBuffer[0] = frame->length;
    crcBuffer[1] = frame->cmd_id;

    if (frame->length > 0U)
    {
        memcpy(&crcBuffer[2], frame->payload, frame->length);
    }

    return SPS_Protocol_Crc16Ccitt(crcBuffer, (uint16_t)(2U + frame->length));
}

SPS_ParseResult_t SPS_Protocol_ParseByte(uint8_t byte, SPS_Frame_t *outFrame)
{
    switch (parserState)
    {
        case PARSER_WAIT_SOF_1:
            if (byte == SPS_SOF_1)
            {
                parserState = PARSER_WAIT_SOF_2;
            }
            break;

        case PARSER_WAIT_SOF_2:
            if (byte == SPS_SOF_2)
            {
                parserState = PARSER_WAIT_LENGTH;
                memset(&currentFrame, 0, sizeof(currentFrame));
                payloadIndex = 0U;
                receivedCrc = 0U;
            }
            else if (byte == SPS_SOF_1)
            {
                /*
                 * Vẫn giữ trạng thái chờ SOF_2.
                 * Trường hợp stream có AA AA 55 thì byte AA thứ hai
                 * được xem như SOF_1 mới.
                 */
                parserState = PARSER_WAIT_SOF_2;
            }
            else
            {
                parserState = PARSER_WAIT_SOF_1;
            }
            break;

        case PARSER_WAIT_LENGTH:
            currentFrame.length = byte;
            parserState = PARSER_WAIT_CMD;
            break;

        case PARSER_WAIT_CMD:
            currentFrame.cmd_id = byte;

            if (currentFrame.length == 0U)
            {
                parserState = PARSER_WAIT_CRC_LOW;
            }
            else
            {
                parserState = PARSER_WAIT_PAYLOAD;
            }
            break;

        case PARSER_WAIT_PAYLOAD:
            currentFrame.payload[payloadIndex] = byte;
            payloadIndex++;

            if (payloadIndex >= currentFrame.length)
            {
                parserState = PARSER_WAIT_CRC_LOW;
            }
            break;

        case PARSER_WAIT_CRC_LOW:
            receivedCrc = byte;
            parserState = PARSER_WAIT_CRC_HIGH;
            break;

        case PARSER_WAIT_CRC_HIGH:
        {
            uint16_t calculatedCrc;

            receivedCrc |= ((uint16_t)byte << 8);
            calculatedCrc = SPS_Protocol_CalcFrameCrc(&currentFrame);

            if (receivedCrc == calculatedCrc)
            {
                if (outFrame != NULL)
                {
                    memcpy(outFrame, &currentFrame, sizeof(SPS_Frame_t));
                }

                parserState = PARSER_WAIT_SOF_1;
                payloadIndex = 0U;
                receivedCrc = 0U;

                return SPS_PARSE_FRAME_READY;
            }

            parserState = PARSER_WAIT_SOF_1;
            payloadIndex = 0U;
            receivedCrc = 0U;

            return SPS_PARSE_ERROR;
        }

        default:
            parserState = PARSER_WAIT_SOF_1;
            payloadIndex = 0U;
            receivedCrc = 0U;
            break;
    }

    return SPS_PARSE_IN_PROGRESS;
}

uint16_t SPS_Protocol_BuildFrame(uint8_t cmd_id,
                                 const uint8_t *payload,
                                 uint8_t length,
                                 uint8_t *outBuffer,
                                 uint16_t outBufferSize)
{
    uint16_t index = 0U;
    uint16_t crc;
    uint8_t crcBuffer[2U + SPS_MAX_PAYLOAD_SIZE];

    if (outBuffer == NULL)
    {
        return 0U;
    }

    if ((payload == NULL) && (length > 0U))
    {
        return 0U;
    }

    if (outBufferSize < (uint16_t)(2U + 1U + 1U + length + 2U))
    {
        return 0U;
    }

    outBuffer[index++] = SPS_SOF_1;
    outBuffer[index++] = SPS_SOF_2;
    outBuffer[index++] = length;
    outBuffer[index++] = cmd_id;

    if (length > 0U)
    {
        memcpy(&outBuffer[index], payload, length);
        index = (uint16_t)(index + length);
    }

    crcBuffer[0] = length;
    crcBuffer[1] = cmd_id;

    if (length > 0U)
    {
        memcpy(&crcBuffer[2], payload, length);
    }

    crc = SPS_Protocol_Crc16Ccitt(crcBuffer, (uint16_t)(2U + length));

    outBuffer[index++] = (uint8_t)(crc & 0xFFU);
    outBuffer[index++] = (uint8_t)((crc >> 8) & 0xFFU);

    return index;
}
