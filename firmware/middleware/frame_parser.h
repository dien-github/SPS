#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

#include "sps_error.h"
#include "sps_protocol.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    FRAME_PARSER_IN_PROGRESS = 0,
    FRAME_PARSER_FRAME_READY,
    FRAME_PARSER_ERROR
} frame_parser_result_t;

typedef struct
{
    uint8_t last_seq;
    uint8_t last_msg_type;
    uint8_t last_device_id;
    uint8_t last_command_id;
    sps_error_code_t last_error;
} frame_parser_error_t;

typedef struct
{
    uint8_t state;
    uint16_t index;
    uint16_t expected_payload_len;
    uint8_t crc_bytes[2];
    sps_frame_t frame;
    frame_parser_error_t error;
} frame_parser_t;

void frame_parser_init(frame_parser_t *parser);
frame_parser_result_t frame_parser_push(frame_parser_t *parser, uint8_t byte, sps_frame_t *out_frame);
const frame_parser_error_t *frame_parser_get_error(const frame_parser_t *parser);
uint16_t frame_builder_build(const sps_frame_t *frame, uint8_t *out, uint16_t out_size);

#endif
