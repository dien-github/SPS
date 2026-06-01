#include "frame_parser.h"
#include "crc16_ccitt.h"
#include <string.h>

typedef enum
{
    PARSER_SOF0 = 0,
    PARSER_SOF1,
    PARSER_VERSION,
    PARSER_SEQ,
    PARSER_MSG_TYPE,
    PARSER_DEVICE,
    PARSER_COMMAND,
    PARSER_LEN0,
    PARSER_LEN1,
    PARSER_PAYLOAD,
    PARSER_CRC0,
    PARSER_CRC1
} parser_state_t;

static void parser_reset(frame_parser_t *parser)
{
    parser->state = (uint8_t)PARSER_SOF0;
    parser->index = 0u;
    parser->expected_payload_len = 0u;
    parser->crc_bytes[0] = 0u;
    parser->crc_bytes[1] = 0u;
}

static frame_parser_result_t parser_error(frame_parser_t *parser, sps_error_code_t err)
{
    parser->error.last_seq = parser->frame.seq;
    parser->error.last_msg_type = parser->frame.msg_type;
    parser->error.last_device_id = parser->frame.device_id;
    parser->error.last_command_id = parser->frame.command_id;
    parser->error.last_error = err;
    parser_reset(parser);
    return FRAME_PARSER_ERROR;
}

void frame_parser_init(frame_parser_t *parser)
{
    if (parser == 0)
    {
        return;
    }

    memset(parser, 0, sizeof(*parser));
    parser_reset(parser);
}

frame_parser_result_t frame_parser_push(frame_parser_t *parser, uint8_t byte, sps_frame_t *out_frame)
{
    if (parser == 0)
    {
        return FRAME_PARSER_ERROR;
    }

    switch ((parser_state_t)parser->state)
    {
        case PARSER_SOF0:
            if (byte == SPS_PROTO_SOF0)
            {
                parser->state = (uint8_t)PARSER_SOF1;
            }
            break;

        case PARSER_SOF1:
            if (byte == SPS_PROTO_SOF1)
            {
                memset(&parser->frame, 0, sizeof(parser->frame));
                parser->state = (uint8_t)PARSER_VERSION;
            }
            else if (byte != SPS_PROTO_SOF0)
            {
                parser->state = (uint8_t)PARSER_SOF0;
            }
            break;

        case PARSER_VERSION:
            parser->frame.version = byte;
            if (byte != SPS_PROTO_VERSION)
            {
                return parser_error(parser, SPS_ERR_BAD_VERSION);
            }
            parser->state = (uint8_t)PARSER_SEQ;
            break;

        case PARSER_SEQ:
            parser->frame.seq = byte;
            parser->state = (uint8_t)PARSER_MSG_TYPE;
            break;

        case PARSER_MSG_TYPE:
            parser->frame.msg_type = byte;
            parser->state = (uint8_t)PARSER_DEVICE;
            break;

        case PARSER_DEVICE:
            parser->frame.device_id = byte;
            parser->state = (uint8_t)PARSER_COMMAND;
            break;

        case PARSER_COMMAND:
            parser->frame.command_id = byte;
            parser->state = (uint8_t)PARSER_LEN0;
            break;

        case PARSER_LEN0:
            parser->expected_payload_len = byte;
            parser->state = (uint8_t)PARSER_LEN1;
            break;

        case PARSER_LEN1:
            parser->expected_payload_len |= (uint16_t)byte << 8;
            parser->frame.payload_len = parser->expected_payload_len;
            parser->index = 0u;
            if (parser->expected_payload_len > SPS_FRAME_MAX_PAYLOAD)
            {
                return parser_error(parser, SPS_ERR_BAD_LENGTH);
            }
            parser->state = (parser->expected_payload_len == 0u) ? (uint8_t)PARSER_CRC0 : (uint8_t)PARSER_PAYLOAD;
            break;

        case PARSER_PAYLOAD:
            parser->frame.payload[parser->index++] = byte;
            if (parser->index >= parser->expected_payload_len)
            {
                parser->state = (uint8_t)PARSER_CRC0;
            }
            break;

        case PARSER_CRC0:
            parser->crc_bytes[0] = byte;
            parser->state = (uint8_t)PARSER_CRC1;
            break;

        case PARSER_CRC1:
        {
            uint8_t crc_input[SPS_PROTO_HEADER_LEN_NO_SOF + SPS_FRAME_MAX_PAYLOAD];
            uint16_t rx_crc;
            uint16_t calc_crc;

            parser->crc_bytes[1] = byte;
            crc_input[0] = parser->frame.version;
            crc_input[1] = parser->frame.seq;
            crc_input[2] = parser->frame.msg_type;
            crc_input[3] = parser->frame.device_id;
            crc_input[4] = parser->frame.command_id;
            crc_input[5] = (uint8_t)(parser->frame.payload_len & 0xFFu);
            crc_input[6] = (uint8_t)(parser->frame.payload_len >> 8);
            if (parser->frame.payload_len > 0u)
            {
                memcpy(&crc_input[7], parser->frame.payload, parser->frame.payload_len);
            }

            rx_crc = (uint16_t)parser->crc_bytes[0] | ((uint16_t)parser->crc_bytes[1] << 8);
            calc_crc = crc16_ccitt_false(crc_input, (size_t)SPS_PROTO_HEADER_LEN_NO_SOF + parser->frame.payload_len);
            if (rx_crc != calc_crc)
            {
                return parser_error(parser, SPS_ERR_BAD_CRC);
            }

            if (out_frame != 0)
            {
                *out_frame = parser->frame;
            }
            parser_reset(parser);
            return FRAME_PARSER_FRAME_READY;
        }

        default:
            parser_reset(parser);
            break;
    }

    return FRAME_PARSER_IN_PROGRESS;
}

const frame_parser_error_t *frame_parser_get_error(const frame_parser_t *parser)
{
    return (parser == 0) ? 0 : &parser->error;
}

uint16_t frame_builder_build(const sps_frame_t *frame, uint8_t *out, uint16_t out_size)
{
    uint8_t crc_input[SPS_PROTO_HEADER_LEN_NO_SOF + SPS_FRAME_MAX_PAYLOAD];
    uint16_t crc;
    uint16_t pos = 0u;

    if ((frame == 0) || (out == 0) || (frame->payload_len > SPS_FRAME_MAX_PAYLOAD))
    {
        return 0u;
    }

    if (out_size < (uint16_t)(2u + SPS_PROTO_HEADER_LEN_NO_SOF + frame->payload_len + 2u))
    {
        return 0u;
    }

    crc_input[0] = frame->version;
    crc_input[1] = frame->seq;
    crc_input[2] = frame->msg_type;
    crc_input[3] = frame->device_id;
    crc_input[4] = frame->command_id;
    crc_input[5] = (uint8_t)(frame->payload_len & 0xFFu);
    crc_input[6] = (uint8_t)(frame->payload_len >> 8);
    if (frame->payload_len > 0u)
    {
        memcpy(&crc_input[7], frame->payload, frame->payload_len);
    }
    crc = crc16_ccitt_false(crc_input, (size_t)SPS_PROTO_HEADER_LEN_NO_SOF + frame->payload_len);

    out[pos++] = SPS_PROTO_SOF0;
    out[pos++] = SPS_PROTO_SOF1;
    memcpy(&out[pos], crc_input, (size_t)SPS_PROTO_HEADER_LEN_NO_SOF + frame->payload_len);
    pos = (uint16_t)(pos + SPS_PROTO_HEADER_LEN_NO_SOF + frame->payload_len);
    out[pos++] = (uint8_t)(crc & 0xFFu);
    out[pos++] = (uint8_t)(crc >> 8);
    return pos;
}
