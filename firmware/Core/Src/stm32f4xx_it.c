#include "timebase.h"
#include "uart_ll.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"

extern void xPortSysTickHandler(void);

void SysTick_Handler(void)
{
    timebase_tick_isr();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();
    }
}

void USART1_IRQHandler(void)
{
    uart_ll_irq_handler(UART_LL_PORT_SBC);
}

void USART2_IRQHandler(void)
{
    uart_ll_irq_handler(UART_LL_PORT_PROJECTOR);
}

void NMI_Handler(void)
{
    for (;;)
    {
    }
}

void HardFault_Handler(void)
{
    for (;;)
    {
    }
}

void MemManage_Handler(void)
{
    for (;;)
    {
    }
}

void BusFault_Handler(void)
{
    for (;;)
    {
    }
}

void UsageFault_Handler(void)
{
    for (;;)
    {
    }
}
