#include "timer_ll.h"
#include "sps_config.h"
#include "stm32f4xx.h"

void timer_ll_init_cycle_counter(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t timer_ll_cycles(void)
{
    return DWT->CYCCNT;
}

void timer_ll_delay_us(uint32_t us)
{
    const uint32_t cycles_per_us = SPS_SYSTEM_CORE_CLOCK_HZ / 1000000u;
    uint32_t start = DWT->CYCCNT;
    uint32_t wait_cycles = us * cycles_per_us;

    while ((uint32_t)(DWT->CYCCNT - start) < wait_cycles)
    {
        __asm volatile ("nop");
    }
}
