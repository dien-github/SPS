#include "app_queue.h"

static int q;

void Queue_Init(void)
{
    q = 0;
}

void Queue_Send(int data)
{
    q = data;
}

int Queue_Receive(void)
{
    return q;
}