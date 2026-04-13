
#include "app.h"
#include "app_tasks.h"
#include "app_queue.h"
#include "uart.h"

void App_Init(void)
{
    Queue_Init();
    UART_Send("System Init\n");
}

void App_Run(void)
{
    Task_IR();
    Task_Relay();
}