#ifndef RELAY_SERVICE_H
#define RELAY_SERVICE_H

#include <stdint.h>

/*
 * Relay command packed format - uint32_t
 *
 * [31:24] source command id
 * [23:16] relay type
 * [15:08] device id
 * [07:00] action
 */

#define RELAY_CMD_SOURCE_SHIFT      24U
#define RELAY_CMD_TYPE_SHIFT        16U
#define RELAY_CMD_DEVICE_SHIFT      8U
#define RELAY_CMD_ACTION_SHIFT      0U

#define RELAY_CMD_GET_SOURCE(cmd)   ((uint8_t)(((cmd) >> RELAY_CMD_SOURCE_SHIFT) & 0xFFU))
#define RELAY_CMD_GET_TYPE(cmd)     ((uint8_t)(((cmd) >> RELAY_CMD_TYPE_SHIFT) & 0xFFU))
#define RELAY_CMD_GET_DEVICE(cmd)   ((uint8_t)(((cmd) >> RELAY_CMD_DEVICE_SHIFT) & 0xFFU))
#define RELAY_CMD_GET_ACTION(cmd)   ((uint8_t)(((cmd) >> RELAY_CMD_ACTION_SHIFT) & 0xFFU))

#define RELAY_TYPE_LIGHT            0x01U
#define RELAY_TYPE_MOTOR            0x02U

/*
 * Light device id
 * Theo Midterm:
 * 0x01: đèn bục / đèn chính demo
 * 0xFF: tất cả đèn
 */
#define RELAY_DEVICE_LIGHT_1        0x01U
#define RELAY_DEVICE_LIGHT_ALL      0xFFU

/*
 * Motor device id
 * 0x01: rèm cửa
 * 0x02: màn chiếu
 */
#define RELAY_DEVICE_CURTAIN        0x01U
#define RELAY_DEVICE_SCREEN         0x02U

/*
 * Light action
 */
#define RELAY_ACTION_OFF            0x00U
#define RELAY_ACTION_ON             0x01U

/*
 * Motor action
 * 0x01: mở / lên
 * 0x00: đóng / xuống
 * 0x02: dừng
 */
#define RELAY_ACTION_CLOSE          0x00U
#define RELAY_ACTION_OPEN           0x01U
#define RELAY_ACTION_STOP           0x02U

typedef enum
{
    RELAY_RESULT_OK = 0,
    RELAY_RESULT_INVALID_PARAM
} RelayResult_t;

void Relay_Service_Init(void);
RelayResult_t Relay_Service_ExecuteCommand(uint32_t relayCmd);
void Relay_Service_Periodic(void);

uint8_t Relay_Service_GetLightState(void);
uint8_t Relay_Service_GetCurtainState(void);
uint8_t Relay_Service_GetScreenState(void);

#endif /* RELAY_SERVICE_H */
