#include "ir_service.h"

#include "tim.h"
#include "cmsis_os.h"

#define IR_BURST_MS  100U

void IR_Service_Init(void)
{
    /*
     * TIM1 PWM CH1 đã được cấu hình khoảng 37–38kHz.
     * Mặc định tắt PWM khi khởi động.
     */
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
}

static void IR_SendDemoBurst(void)
{
    /*
     * Skeleton:
     * Hiện tại chỉ phát thử carrier 38kHz trong 100ms.
     * Sau này thay bằng IR pattern thật của máy lạnh.
     */
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    osDelay(IR_BURST_MS);
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
}

IrResult_t IR_Service_ControlAC(uint8_t acId, uint8_t action)
{
    if (acId == 0U)
    {
        return IR_RESULT_INVALID_PARAM;
    }

    if ((action != IR_AC_ACTION_ON) &&
        (action != IR_AC_ACTION_OFF))
    {
        return IR_RESULT_INVALID_PARAM;
    }

    IR_SendDemoBurst();

    return IR_RESULT_OK;
}
IrResult_t IR_Service_ExecuteCommand(const IrCommand_t *cmd)
{
    if (cmd == NULL)
    {
        return IR_RESULT_INVALID_PARAM;
    }

    return IR_Service_ControlAC(cmd->acId, cmd->action);
}
