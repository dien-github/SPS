#include "app_tasks.h"
#include "ir.h"
#include "relay.h"
#include "app_queue.h"

void Task_IR(void)
{
    int data = IR_Read();
    Queue_Send(data);
}

void Task_Relay(void)
{
    int cmd = Queue_Receive();

    if (cmd == 1)
        RELAY_On();
    else
        RELAY_Off();
}