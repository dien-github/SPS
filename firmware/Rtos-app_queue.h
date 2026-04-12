#ifndef APP_QUEUE_H
#define APP_QUEUE_H

void Queue_Init(void);
void Queue_Send(int data);
int Queue_Receive(void);

#endif