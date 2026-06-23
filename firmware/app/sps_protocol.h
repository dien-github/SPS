#ifndef SPS_PROTOCOL_H
#define SPS_PROTOCOL_H

#include "sps_config.h"
#include <stdint.h>

#define SPS_PROTO_SOF0                 0xA5u
#define SPS_PROTO_SOF1                 0x5Au
#define SPS_PROTO_VERSION              0x01u
#define SPS_PROTO_HEADER_LEN_NO_SOF    7u
#define SPS_PROTO_CRC_LEN              2u
#define SPS_PROTO_MAX_FRAME            (2u + SPS_PROTO_HEADER_LEN_NO_SOF + SPS_FRAME_MAX_PAYLOAD + SPS_PROTO_CRC_LEN)

typedef enum
{
    SPS_MSG_COMMAND = 0x01,
    SPS_MSG_RESPONSE = 0x02,
    SPS_MSG_TELEMETRY = 0x03,
    SPS_MSG_ERROR = 0x04,
    SPS_MSG_OTA_START = 0x10,
    SPS_MSG_OTA_BLOCK = 0x11,
    SPS_MSG_OTA_END = 0x12,
    SPS_MSG_OTA_ABORT = 0x13,
    SPS_MSG_PING = 0x7E,
    SPS_MSG_PONG = 0x7F
} sps_msg_type_t;

typedef enum
{
    SPS_DEV_SYSTEM = 0x00,
    SPS_DEV_PROJECTOR = 0x01,
    SPS_DEV_LIGHT = 0x02,
    SPS_DEV_CURTAIN = 0x03,
    SPS_DEV_SCREEN = 0x04,
    SPS_DEV_AC_IR = 0x05,
    SPS_DEV_OTA = 0x06,
    SPS_DEV_RELAY_RAW = 0x07,
    SPS_DEV_COMPUTER_WOL_UNSUPPORTED_ON_MCU = 0x08
} sps_device_id_t;

typedef enum
{
    SPS_SYS_GET_VERSION = 0x01,
    SPS_SYS_GET_STATUS = 0x02,
    SPS_SYS_RESET_MCU = 0x03,
    SPS_SYS_SET_TIME_OPTIONAL = 0x04,
    SPS_SYS_PING = 0x05
} sps_system_cmd_t;

typedef enum
{
    SPS_PROJECTOR_POWER_ON = 0x01,
    SPS_PROJECTOR_POWER_OFF = 0x02,
    SPS_PROJECTOR_GET_POWER_STATUS = 0x03,
    SPS_PROJECTOR_SOURCE_HDMI = 0x04,
    SPS_PROJECTOR_SOURCE_VGA = 0x05,
    SPS_PROJECTOR_MUTE_ON = 0x06,
    SPS_PROJECTOR_MUTE_OFF = 0x07,
    SPS_PROJECTOR_GET_ERROR_STATUS = 0x08
} sps_projector_cmd_t;

typedef enum
{
    SPS_LIGHT_ON = 0x01,
    SPS_LIGHT_OFF = 0x02,
    SPS_LIGHT_TOGGLE = 0x03,
    SPS_LIGHT_GET_STATE = 0x04
} sps_light_cmd_t;

typedef enum
{
    SPS_CURTAIN_OPEN = 0x01,
    SPS_CURTAIN_CLOSE = 0x02,
    SPS_CURTAIN_STOP = 0x03,
    SPS_CURTAIN_GET_STATE = 0x04
} sps_curtain_cmd_t;

typedef enum
{
    SPS_SCREEN_UP = 0x01,
    SPS_SCREEN_DOWN = 0x02,
    SPS_SCREEN_STOP = 0x03,
    SPS_SCREEN_GET_STATE = 0x04
} sps_screen_cmd_t;

typedef enum
{
    SPS_AC_ON = 0x01,
    SPS_AC_OFF = 0x02,
    SPS_AC_SET_TEMP = 0x03,
    SPS_AC_COOL_MODE = 0x04,
    SPS_AC_FAN_MODE = 0x05,
    SPS_AC_DRY_MODE = 0x06,
    SPS_AC_GET_ASSUMED_STATE = 0x07
} sps_ac_cmd_t;

typedef enum
{
    SPS_RELAY_RAW_SET_CHANNEL = 0x01,
    SPS_RELAY_RAW_GET_CHANNEL = 0x02,
    SPS_RELAY_RAW_ALL_OFF = 0x03
} sps_relay_raw_cmd_t;

typedef struct
{
    uint8_t version;
    uint8_t seq;
    uint8_t msg_type;
    uint8_t device_id;
    uint8_t command_id;
    uint16_t payload_len;
    uint8_t payload[SPS_FRAME_MAX_PAYLOAD];
} sps_frame_t;

#endif
