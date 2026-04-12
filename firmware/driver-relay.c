#include "relay.h"
#include "gpio.h"

#define RELAY_PIN 1

void RELAY_On(void)
{
    GPIO_Write(RELAY_PIN, 1);
}

void RELAY_Off(void)
{
    GPIO_Write(RELAY_PIN, 0);
}