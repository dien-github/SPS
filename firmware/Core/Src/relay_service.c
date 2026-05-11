#include "relay_service.h"

#include "main.h"
#include "cmsis_os.h"

/*
 * Nếu module relay của bạn là active-low thì đổi:
 *
 * RELAY_GPIO_ON  = GPIO_PIN_RESET
 * RELAY_GPIO_OFF = GPIO_PIN_SET
 *
 * Hiện tại đang dùng logic:
 * GPIO HIGH = relay ON
 * GPIO LOW  = relay OFF
 */
#define RELAY_GPIO_ON               GPIO_PIN_SET
#define RELAY_GPIO_OFF              GPIO_PIN_RESET

#define MOTOR_SAFE_SWITCH_DELAY_MS  150U
#define MOTOR_AUTO_STOP_MS          25000U

typedef enum
{
    MOTOR_STATE_STOP = 0,
    MOTOR_STATE_OPENING,
    MOTOR_STATE_CLOSING
} MotorState_t;

typedef struct
{
    MotorState_t state;
    uint32_t startTick;
} MotorControl_t;

static uint8_t lightState;
static MotorControl_t curtainMotor;
static MotorControl_t screenMotor;

static void Relay_Write(GPIO_TypeDef *port, uint16_t pin, uint8_t on);
static void Motor_StopCurtain(void);
static void Motor_StopScreen(void);

static RelayResult_t Relay_ControlLight(uint8_t deviceId, uint8_t action);
static RelayResult_t Relay_ControlMotor(uint8_t deviceId, uint8_t action);

static void Relay_Write(GPIO_TypeDef *port, uint16_t pin, uint8_t on)
{
    HAL_GPIO_WritePin(port,
                      pin,
                      (on != 0U) ? RELAY_GPIO_ON : RELAY_GPIO_OFF);
}

static void Motor_StopCurtain(void)
{
    Relay_Write(CURTAIN_OPEN_RELAY_GPIO_Port, CURTAIN_OPEN_RELAY_Pin, 0U);
    Relay_Write(CURTAIN_CLOSE_RELAY_GPIO_Port, CURTAIN_CLOSE_RELAY_Pin, 0U);

    curtainMotor.state = MOTOR_STATE_STOP;
}

static void Motor_StopScreen(void)
{
    Relay_Write(SCREEN_UP_RELAY_GPIO_Port, SCREEN_UP_RELAY_Pin, 0U);
    Relay_Write(SCREEN_DOWN_RELAY_GPIO_Port, SCREEN_DOWN_RELAY_Pin, 0U);

    screenMotor.state = MOTOR_STATE_STOP;
}

void Relay_Service_Init(void)
{
    lightState = RELAY_ACTION_OFF;

    curtainMotor.state = MOTOR_STATE_STOP;
    curtainMotor.startTick = 0U;

    screenMotor.state = MOTOR_STATE_STOP;
    screenMotor.startTick = 0U;

    Relay_Write(LIGHT_RELAY_GPIO_Port, LIGHT_RELAY_Pin, 0U);
    Motor_StopCurtain();
    Motor_StopScreen();
}

static RelayResult_t Relay_ControlLight(uint8_t deviceId, uint8_t action)
{
    if ((deviceId != RELAY_DEVICE_LIGHT_1) &&
        (deviceId != RELAY_DEVICE_LIGHT_ALL))
    {
        return RELAY_RESULT_INVALID_PARAM;
    }

    if ((action != RELAY_ACTION_ON) &&
        (action != RELAY_ACTION_OFF))
    {
        return RELAY_RESULT_INVALID_PARAM;
    }

    Relay_Write(LIGHT_RELAY_GPIO_Port, LIGHT_RELAY_Pin, action);
    lightState = action;

    return RELAY_RESULT_OK;
}

static RelayResult_t Relay_ControlMotor(uint8_t deviceId, uint8_t action)
{
    MotorControl_t *motor = NULL;

    if ((action != RELAY_ACTION_OPEN) &&
        (action != RELAY_ACTION_CLOSE) &&
        (action != RELAY_ACTION_STOP))
    {
        return RELAY_RESULT_INVALID_PARAM;
    }

    if (deviceId == RELAY_DEVICE_CURTAIN)
    {
        motor = &curtainMotor;

        /*
         * Chống kích 2 chiều cùng lúc.
         * Luôn tắt cả 2 relay trước khi đổi hướng.
         */
        Motor_StopCurtain();

        if (action == RELAY_ACTION_STOP)
        {
            return RELAY_RESULT_OK;
        }

        osDelay(MOTOR_SAFE_SWITCH_DELAY_MS);

        if (action == RELAY_ACTION_OPEN)
        {
            Relay_Write(CURTAIN_OPEN_RELAY_GPIO_Port,
                        CURTAIN_OPEN_RELAY_Pin,
                        1U);

            motor->state = MOTOR_STATE_OPENING;
        }
        else
        {
            Relay_Write(CURTAIN_CLOSE_RELAY_GPIO_Port,
                        CURTAIN_CLOSE_RELAY_Pin,
                        1U);

            motor->state = MOTOR_STATE_CLOSING;
        }

        motor->startTick = osKernelGetTickCount();

        return RELAY_RESULT_OK;
    }

    if (deviceId == RELAY_DEVICE_SCREEN)
    {
        motor = &screenMotor;

        /*
         * Chống kích 2 chiều cùng lúc.
         * Luôn tắt cả 2 relay trước khi đổi hướng.
         */
        Motor_StopScreen();

        if (action == RELAY_ACTION_STOP)
        {
            return RELAY_RESULT_OK;
        }

        osDelay(MOTOR_SAFE_SWITCH_DELAY_MS);

        if (action == RELAY_ACTION_OPEN)
        {
            /*
             * Với màn chiếu:
             * OPEN được map thành UP.
             */
            Relay_Write(SCREEN_UP_RELAY_GPIO_Port,
                        SCREEN_UP_RELAY_Pin,
                        1U);

            motor->state = MOTOR_STATE_OPENING;
        }
        else
        {
            /*
             * CLOSE được map thành DOWN.
             */
            Relay_Write(SCREEN_DOWN_RELAY_GPIO_Port,
                        SCREEN_DOWN_RELAY_Pin,
                        1U);

            motor->state = MOTOR_STATE_CLOSING;
        }

        motor->startTick = osKernelGetTickCount();

        return RELAY_RESULT_OK;
    }

    return RELAY_RESULT_INVALID_PARAM;
}

RelayResult_t Relay_Service_ExecuteCommand(uint32_t relayCmd)
{
    uint8_t type;
    uint8_t deviceId;
    uint8_t action;

    type = RELAY_CMD_GET_TYPE(relayCmd);
    deviceId = RELAY_CMD_GET_DEVICE(relayCmd);
    action = RELAY_CMD_GET_ACTION(relayCmd);

    if (type == RELAY_TYPE_LIGHT)
    {
        return Relay_ControlLight(deviceId, action);
    }

    if (type == RELAY_TYPE_MOTOR)
    {
        return Relay_ControlMotor(deviceId, action);
    }

    return RELAY_RESULT_INVALID_PARAM;
}

void Relay_Service_Periodic(void)
{
    uint32_t now;

    now = osKernelGetTickCount();

    if ((curtainMotor.state != MOTOR_STATE_STOP) &&
        ((now - curtainMotor.startTick) >= MOTOR_AUTO_STOP_MS))
    {
        Motor_StopCurtain();
    }

    if ((screenMotor.state != MOTOR_STATE_STOP) &&
        ((now - screenMotor.startTick) >= MOTOR_AUTO_STOP_MS))
    {
        Motor_StopScreen();
    }
}

uint8_t Relay_Service_GetLightState(void)
{
    return lightState;
}

uint8_t Relay_Service_GetCurtainState(void)
{
    return (uint8_t)curtainMotor.state;
}

uint8_t Relay_Service_GetScreenState(void)
{
    return (uint8_t)screenMotor.state;
}
