#include "projector_service.h"

#include "usart.h"

#include <string.h>

#define PROJECTOR_UART_TIMEOUT_MS  100U

/*
 * Skeleton RS232 command.
 *
 * Lưu ý:
 * - Đây là chuỗi demo.
 * - Khi có model máy chiếu thật, cần thay bằng command đúng theo RS232 protocol manual.
 * - USART2 đang được cấu hình 9600 8N1, phù hợp với đa số projector RS232.
 */
static const uint8_t projectorPowerOnCmd[]  = "PWR ON\r";
static const uint8_t projectorPowerOffCmd[] = "PWR OFF\r";

void Projector_Service_Init(void)
{
    /*
     * USART2 đã được init trong MX_USART2_UART_Init().
     * Hiện tại không cần xử lý thêm.
     */
}

ProjectorResult_t Projector_Service_Control(uint8_t action)
{
    HAL_StatusTypeDef status;

    if (action == PROJECTOR_ACTION_ON)
    {
        status = HAL_UART_Transmit(&huart2,
                                   (uint8_t *)projectorPowerOnCmd,
                                   (uint16_t)strlen((const char *)projectorPowerOnCmd),
                                   PROJECTOR_UART_TIMEOUT_MS);
    }
    else if (action == PROJECTOR_ACTION_OFF)
    {
        status = HAL_UART_Transmit(&huart2,
                                   (uint8_t *)projectorPowerOffCmd,
                                   (uint16_t)strlen((const char *)projectorPowerOffCmd),
                                   PROJECTOR_UART_TIMEOUT_MS);
    }
    else
    {
        return PROJECTOR_RESULT_INVALID_PARAM;
    }

    if (status != HAL_OK)
    {
        return PROJECTOR_RESULT_UART_ERROR;
    }

    return PROJECTOR_RESULT_OK;
}
ProjectorResult_t Projector_Service_ExecuteCommand(const ProjectorCommand_t *cmd)
{
    if (cmd == NULL)
    {
        return PROJECTOR_RESULT_INVALID_PARAM;
    }

    return Projector_Service_Control(cmd->action);
}
