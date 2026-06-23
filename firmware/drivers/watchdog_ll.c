#include "watchdog_ll.h"
#include "stm32f4xx.h"

void watchdog_ll_init(uint32_t timeout_ms)
{
    uint32_t reload;

    if (timeout_ms == 0u)
    {
        return;
    }

    reload = (timeout_ms * 32000u) / (64u * 1000u);
    if (reload > 0x0FFFu)
    {
        reload = 0x0FFFu;
    }
    if (reload == 0u)
    {
        reload = 1u;
    }

    IWDG->KR = 0x5555u;
    IWDG->PR = 0x04u;
    IWDG->RLR = reload;
    IWDG->KR = 0xAAAAu;
    IWDG->KR = 0xCCCCu;
}

void watchdog_ll_feed(void)
{
    IWDG->KR = 0xAAAAu;
}
