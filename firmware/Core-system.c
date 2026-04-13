#include "system.h"
#include "uart.h"
#include "gpio.h"

void System_Init(void)
{
    UART_Init();
    GPIO_Init();
}