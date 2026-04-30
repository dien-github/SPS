#include "relay_service.h"
#include "stm32f4xx_ll_gpio.h"
#include "cmsis_os.h"

#define RELAY_ACTIVE_HIGH          1U

#define RELAY_ON                   1U
#define RELAY_OFF                  0U

#define MOTOR_SAFE_DELAY_MS        150U
#define MOTOR_TIMEOUT_MS           25000U

#define RELAY_LIGHT_PORT           GPIOB
#define RELAY_LIGHT_PIN            LL_GPIO_PIN_12

#define RELAY_CURTAIN_OPEN_PORT    GPIOB
#define RELAY_CURTAIN_OPEN_PIN     LL_GPIO_PIN_13

#define RELAY_CURTAIN_CLOSE_PORT   GPIOB
#define RELAY_CURTAIN_CLOSE_PIN    LL_GPIO_PIN_14

#define RELAY_SCREEN_UP_PORT       GPIOB
#define RELAY_SCREEN_UP_PIN        LL_GPIO_PIN_15

#define RELAY_SCREEN_DOWN_PORT     GPIOA
#define RELAY_SCREEN_DOWN_PIN      LL_GPIO_PIN_15

static uint8_t light_status = RELAY_STATUS_OFF_STOP;
static uint8_t curtain_status = RELAY_STATUS_OFF_STOP;
static uint8_t screen_status = RELAY_STATUS_OFF_STOP;

static uint8_t curtain_motor_running = 0U;
static uint8_t screen_motor_running = 0U;

static uint32_t curtain_stop_tick = 0U;
static uint32_t screen_stop_tick = 0U;

static uint32_t Relay_MsToTicks(uint32_t ms)
{
    uint32_t freq = osKernelGetTickFreq();

    if (freq == 0U)
    {
        return ms;
    }

    return ((ms * freq) + 999U) / 1000U;
}

static void Relay_DelayMs(uint32_t ms)
{
    osDelay(Relay_MsToTicks(ms));
}

static uint8_t Relay_TimeExpired(uint32_t now, uint32_t target)
{
    return ((int32_t)(now - target) >= 0);
}

static void Relay_Write(GPIO_TypeDef *port, uint32_t pin, uint8_t state)
{
#if RELAY_ACTIVE_HIGH
    if (state == RELAY_ON)
    {
        LL_GPIO_SetOutputPin(port, pin);
    }
    else
    {
        LL_GPIO_ResetOutputPin(port, pin);
    }
#else
    if (state == RELAY_ON)
    {
        LL_GPIO_ResetOutputPin(port, pin);
    }
    else
    {
        LL_GPIO_SetOutputPin(port, pin);
    }
#endif
}

void Relay_Service_AllOff(void)
{
    Relay_Write(RELAY_LIGHT_PORT, RELAY_LIGHT_PIN, RELAY_OFF);

    Relay_Write(RELAY_CURTAIN_OPEN_PORT, RELAY_CURTAIN_OPEN_PIN, RELAY_OFF);
    Relay_Write(RELAY_CURTAIN_CLOSE_PORT, RELAY_CURTAIN_CLOSE_PIN, RELAY_OFF);

    Relay_Write(RELAY_SCREEN_UP_PORT, RELAY_SCREEN_UP_PIN, RELAY_OFF);
    Relay_Write(RELAY_SCREEN_DOWN_PORT, RELAY_SCREEN_DOWN_PIN, RELAY_OFF);

    light_status = RELAY_STATUS_OFF_STOP;
    curtain_status = RELAY_STATUS_OFF_STOP;
    screen_status = RELAY_STATUS_OFF_STOP;

    curtain_motor_running = 0U;
    screen_motor_running = 0U;
}

void Relay_Service_Init(void)
{
    Relay_Service_AllOff();
}

static relay_result_t Relay_SetLight(uint8_t action)
{
    if (action == RELAY_ACTION_ON_OPEN_UP)
    {
        Relay_Write(RELAY_LIGHT_PORT, RELAY_LIGHT_PIN, RELAY_ON);
        light_status = RELAY_STATUS_ON;
        return RELAY_RESULT_OK;
    }

    if (action == RELAY_ACTION_OFF_CLOSE_DOWN)
    {
        Relay_Write(RELAY_LIGHT_PORT, RELAY_LIGHT_PIN, RELAY_OFF);
        light_status = RELAY_STATUS_OFF_STOP;
        return RELAY_RESULT_OK;
    }

    return RELAY_RESULT_INVALID_ACTION;
}

static relay_result_t Relay_CurtainOpen(void)
{
    Relay_Write(RELAY_CURTAIN_CLOSE_PORT, RELAY_CURTAIN_CLOSE_PIN, RELAY_OFF);
    Relay_DelayMs(MOTOR_SAFE_DELAY_MS);

    Relay_Write(RELAY_CURTAIN_OPEN_PORT, RELAY_CURTAIN_OPEN_PIN, RELAY_ON);

    curtain_status = RELAY_STATUS_OPENING_UP;
    curtain_motor_running = 1U;
    curtain_stop_tick = osKernelGetTickCount() + Relay_MsToTicks(MOTOR_TIMEOUT_MS);

    return RELAY_RESULT_OK;
}

static relay_result_t Relay_CurtainClose(void)
{
    Relay_Write(RELAY_CURTAIN_OPEN_PORT, RELAY_CURTAIN_OPEN_PIN, RELAY_OFF);
    Relay_DelayMs(MOTOR_SAFE_DELAY_MS);

    Relay_Write(RELAY_CURTAIN_CLOSE_PORT, RELAY_CURTAIN_CLOSE_PIN, RELAY_ON);

    curtain_status = RELAY_STATUS_CLOSING_DOWN;
    curtain_motor_running = 1U;
    curtain_stop_tick = osKernelGetTickCount() + Relay_MsToTicks(MOTOR_TIMEOUT_MS);

    return RELAY_RESULT_OK;
}

static relay_result_t Relay_CurtainStop(void)
{
    Relay_Write(RELAY_CURTAIN_OPEN_PORT, RELAY_CURTAIN_OPEN_PIN, RELAY_OFF);
    Relay_Write(RELAY_CURTAIN_CLOSE_PORT, RELAY_CURTAIN_CLOSE_PIN, RELAY_OFF);

    curtain_status = RELAY_STATUS_OFF_STOP;
    curtain_motor_running = 0U;

    return RELAY_RESULT_OK;
}

static relay_result_t Relay_SetCurtain(uint8_t action)
{
    if (action == RELAY_ACTION_ON_OPEN_UP)
    {
        return Relay_CurtainOpen();
    }

    if (action == RELAY_ACTION_OFF_CLOSE_DOWN)
    {
        return Relay_CurtainClose();
    }

    if (action == RELAY_ACTION_STOP)
    {
        return Relay_CurtainStop();
    }

    return RELAY_RESULT_INVALID_ACTION;
}

static relay_result_t Relay_ScreenUp(void)
{
    Relay_Write(RELAY_SCREEN_DOWN_PORT, RELAY_SCREEN_DOWN_PIN, RELAY_OFF);
    Relay_DelayMs(MOTOR_SAFE_DELAY_MS);

    Relay_Write(RELAY_SCREEN_UP_PORT, RELAY_SCREEN_UP_PIN, RELAY_ON);

    screen_status = RELAY_STATUS_OPENING_UP;
    screen_motor_running = 1U;
    screen_stop_tick = osKernelGetTickCount() + Relay_MsToTicks(MOTOR_TIMEOUT_MS);

    return RELAY_RESULT_OK;
}

static relay_result_t Relay_ScreenDown(void)
{
    Relay_Write(RELAY_SCREEN_UP_PORT, RELAY_SCREEN_UP_PIN, RELAY_OFF);
    Relay_DelayMs(MOTOR_SAFE_DELAY_MS);

    Relay_Write(RELAY_SCREEN_DOWN_PORT, RELAY_SCREEN_DOWN_PIN, RELAY_ON);

    screen_status = RELAY_STATUS_CLOSING_DOWN;
    screen_motor_running = 1U;
    screen_stop_tick = osKernelGetTickCount() + Relay_MsToTicks(MOTOR_TIMEOUT_MS);

    return RELAY_RESULT_OK;
}

static relay_result_t Relay_ScreenStop(void)
{
    Relay_Write(RELAY_SCREEN_UP_PORT, RELAY_SCREEN_UP_PIN, RELAY_OFF);
    Relay_Write(RELAY_SCREEN_DOWN_PORT, RELAY_SCREEN_DOWN_PIN, RELAY_OFF);

    screen_status = RELAY_STATUS_OFF_STOP;
    screen_motor_running = 0U;

    return RELAY_RESULT_OK;
}

static relay_result_t Relay_SetScreen(uint8_t action)
{
    if (action == RELAY_ACTION_ON_OPEN_UP)
    {
        return Relay_ScreenUp();
    }

    if (action == RELAY_ACTION_OFF_CLOSE_DOWN)
    {
        return Relay_ScreenDown();
    }

    if (action == RELAY_ACTION_STOP)
    {
        return Relay_ScreenStop();
    }

    return RELAY_RESULT_INVALID_ACTION;
}

relay_result_t Relay_Service_ExecuteCommand(uint32_t packed_cmd)
{
    uint8_t device = RELAY_CMD_GET_DEVICE(packed_cmd);
    uint8_t action = RELAY_CMD_GET_ACTION(packed_cmd);

    switch (device)
    {
        case RELAY_DEV_LIGHT:
            return Relay_SetLight(action);

        case RELAY_DEV_CURTAIN:
            return Relay_SetCurtain(action);

        case RELAY_DEV_SCREEN:
            return Relay_SetScreen(action);

        default:
            return RELAY_RESULT_INVALID_DEVICE;
    }
}

void Relay_Service_Periodic(void)
{
    uint32_t now = osKernelGetTickCount();

    if (curtain_motor_running != 0U)
    {
        if (Relay_TimeExpired(now, curtain_stop_tick))
        {
            (void)Relay_CurtainStop();
        }
    }

    if (screen_motor_running != 0U)
    {
        if (Relay_TimeExpired(now, screen_stop_tick))
        {
            (void)Relay_ScreenStop();
        }
    }
}

uint8_t Relay_Service_GetStatus(uint8_t device)
{
    switch (device)
    {
        case RELAY_DEV_LIGHT:
            return light_status;

        case RELAY_DEV_CURTAIN:
            return curtain_status;

        case RELAY_DEV_SCREEN:
            return screen_status;

        default:
            return 0xFFU;
    }
}
