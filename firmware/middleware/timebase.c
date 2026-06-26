#include "timebase.h"
#include "FreeRTOS.h"
#include "task.h"

static volatile uint32_t timebase_ms;

void timebase_init(void)
{
    timebase_ms = 0u;
}

void timebase_tick_isr(void)
{
    timebase_ms++;
}

uint32_t timebase_millis(void)
{
    return timebase_ms;
}

void timebase_delay_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
    else
    {
        uint32_t start = timebase_millis();
        while ((uint32_t)(timebase_millis() - start) < ms)
        {
            __asm volatile ("nop");
        }
    }
}
