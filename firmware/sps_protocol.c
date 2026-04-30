#include "sps_protocol.h"
#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx.h"

#define CRC16_CCITT_INIT_VALUE      0xFFFFU
#define CRC16_CCITT_POLY            0x1021U

static uint16_t SPS_Crc16UpdateByte(uint16_t crc, uint8_t data)
{
    crc ^= ((uint16_t)data << 8);

    for (uint8_t i = 0; i < 8U; i++)
    {
        if ((crc & 0x8000U) != 0U)
        {
            crc = (uint16_t)((crc << 1) ^ CRC16_CCITT_POLY);
        }
        else
        {
            crc = (uint16_t)(crc << 1);
        }
    }

    return crc;
}

static uint16_t SPS_Crc16Frame(uint8_t length, uint8_t cmd_id, const uint8_t *payload)
{
    uint16_t crc = CRC16_CCITT_INIT_VALUE;

    crc = SPS_Crc16UpdateByte(crc, length);
    crc = SPS_Crc16UpdateByte(crc, cmd_id);

    for (uint8_t i = 0; i < length; i++)
    {
        crc = SPS_Crc16UpdateByte(crc, payload[i]);
    }

    return crc;
}

void SPS_Protocol_ParserInit(sps_parser_t *parser)
{
    parser->state = SPS_PARSE_WAIT_SOF1;
    parser->length = 0U;
    parser->cmd_id = 0U;
    parser->payload_index = 0U;
    parser->crc_calc = CRC16_CCITT_INIT_VALUE;
    parser->crc_rx = 0U;
}

int32_t SPS_Protocol_ParserPushByte(sps_parser_t *parser, uint8_t byte, sps_frame_t *out_frame)
{
    switch (parser->state)
    {
        case SPS_PARSE_WAIT_SOF1:
        {
            if (byte == SPS_SOF_1)
            {
                parser->state = SPS_PARSE_WAIT_SOF2;
            }
            break;
        }

        case SPS_PARSE_WAIT_SOF2:
        {
            if (byte == SPS_SOF_2)
            {
                parser->state = SPS_PARSE_LENGTH;
            }
            else
            {
                parser->state = SPS_PARSE_WAIT_SOF1;
            }
            break;
        }

        case SPS_PARSE_LENGTH:
        {
            parser->length = byte;
            parser->payload_index = 0U;
            parser->crc_calc = CRC16_CCITT_INIT_VALUE;
            parser->crc_calc = SPS_Crc16UpdateByte(parser->crc_calc, byte);
            parser->state = SPS_PARSE_CMD;
            break;
        }

        case SPS_PARSE_CMD:
        {
            parser->cmd_id = byte;
            parser->crc_calc = SPS_Crc16UpdateByte(parser->crc_calc, byte);

            if (parser->length == 0U)
            {
                parser->state = SPS_PARSE_CRC_HIGH;
            }
            else
            {
                parser->state = SPS_PARSE_PAYLOAD;
            }
            break;
        }

        case SPS_PARSE_PAYLOAD:
        {
            parser->payload[parser->payload_index] = byte;
            parser->payload_index++;

            parser->crc_calc = SPS_Crc16UpdateByte(parser->crc_calc, byte);

            if (parser->payload_index >= parser->length)
            {
                parser->state = SPS_PARSE_CRC_HIGH;
            }
            break;
        }

        case SPS_PARSE_CRC_HIGH:
        {
            parser->crc_rx = ((uint16_t)byte << 8);
            parser->state = SPS_PARSE_CRC_LOW;
            break;
        }

        case SPS_PARSE_CRC_LOW:
        {
            parser->crc_rx |= byte;

            if (parser->crc_rx == parser->crc_calc)
            {
                out_frame->length = parser->length;
                out_frame->cmd_id = parser->cmd_id;

                for (uint8_t i = 0; i < parser->length; i++)
                {
                    out_frame->payload[i] = parser->payload[i];
                }

                SPS_Protocol_ParserInit(parser);
                return 1;
            }

            SPS_Protocol_ParserInit(parser);
            return -1;
        }

        default:
        {
            SPS_Protocol_ParserInit(parser);
            break;
        }
    }

    return 0;
}

static void SPS_Protocol_SendByte(uint8_t byte)
{
    while (LL_USART_IsActiveFlag_TXE(USART1) == 0U)
    {
    }

    LL_USART_TransmitData8(USART1, byte);
}

void SPS_Protocol_SendFrame(uint8_t cmd_id, const uint8_t *payload, uint8_t length)
{
    uint16_t crc = SPS_Crc16Frame(length, cmd_id, payload);

    SPS_Protocol_SendByte(SPS_SOF_1);
    SPS_Protocol_SendByte(SPS_SOF_2);
    SPS_Protocol_SendByte(length);
    SPS_Protocol_SendByte(cmd_id);

    for (uint8_t i = 0; i < length; i++)
    {
        SPS_Protocol_SendByte(payload[i]);
    }

    SPS_Protocol_SendByte((uint8_t)((crc >> 8) & 0xFFU));
    SPS_Protocol_SendByte((uint8_t)(crc & 0xFFU));

    while (LL_USART_IsActiveFlag_TC(USART1) == 0U)
    {
    }
}

void SPS_Protocol_SendAck(uint8_t ack_cmd_id)
{
    uint8_t payload[1];

    payload[0] = ack_cmd_id;

    SPS_Protocol_SendFrame(SPS_CMD_ACK, payload, 1U);
}

void SPS_Protocol_SendNack(uint8_t failed_cmd_id, uint8_t error_code)
{
    uint8_t payload[2];

    payload[0] = failed_cmd_id;
    payload[1] = error_code;

    SPS_Protocol_SendFrame(SPS_CMD_NACK, payload, 2U);
}
