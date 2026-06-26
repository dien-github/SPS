#ifndef RELAY_SERVICE_H
#define RELAY_SERVICE_H

#include <stdint.h>

#define RELAY_DEV_LIGHT             0x01U
#define RELAY_DEV_CURTAIN           0x02U
#define RELAY_DEV_SCREEN            0x03U

#define RELAY_ACTION_OFF_CLOSE_DOWN 0x00U
#define RELAY_ACTION_ON_OPEN_UP     0x01U
#define RELAY_ACTION_STOP           0x02U

#define RELAY_STATUS_OFF_STOP       0x00U
#define RELAY_STATUS_ON             0x01U
#define RELAY_STATUS_OPENING_UP     0x02U
#define RELAY_STATUS_CLOSING_DOWN   0x03U

#define RELAY_CMD_PACK(device, action, value, source_cmd) \
    ( ((uint32_t)(device)     & 0xFFU)        | \
     (((uint32_t)(action)     & 0xFFU) << 8)  | \
     (((uint32_t)(value)      & 0xFFU) << 16) | \
     (((uint32_t)(source_cmd) & 0xFFU) << 24) )

#define RELAY_CMD_GET_DEVICE(cmd)      ((uint8_t)((cmd) & 0xFFU))
#define RELAY_CMD_GET_ACTION(cmd)      ((uint8_t)(((cmd) >> 8) & 0xFFU))
#define RELAY_CMD_GET_VALUE(cmd)       ((uint8_t)(((cmd) >> 16) & 0xFFU))
#define RELAY_CMD_GET_SOURCE_CMD(cmd)  ((uint8_t)(((cmd) >> 24) & 0xFFU))

typedef enum
{
    RELAY_RESULT_OK = 0,
    RELAY_RESULT_INVALID_DEVICE,
    RELAY_RESULT_INVALID_ACTION
} relay_result_t;

void Relay_Service_Init(void);
relay_result_t Relay_Service_ExecuteCommand(uint32_t packed_cmd);
void Relay_Service_Periodic(void);
void Relay_Service_AllOff(void);

uint8_t Relay_Service_GetStatus(uint8_t device);

#endif
