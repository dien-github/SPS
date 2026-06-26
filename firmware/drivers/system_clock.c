#include "system_clock.h"
#include "sps_config.h"
#include "stm32f4xx.h"

extern uint32_t SystemCoreClock;

bool system_clock_config_84mhz_hsi(void)
{
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0u)
    {
    }

    if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL)
    {
        RCC->CFGR &= ~RCC_CFGR_SW;
        RCC->CFGR |= RCC_CFGR_SW_HSI;
        while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI)
        {
        }
    }

    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR &= ~PWR_CR_VOS;
    PWR->CR |= PWR_CR_VOS_1;

    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_2WS;

    RCC->CR &= ~RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) != 0u)
    {
    }

    RCC->PLLCFGR = (8u << RCC_PLLCFGR_PLLM_Pos) |
                   (84u << RCC_PLLCFGR_PLLN_Pos) |
                   (0u << RCC_PLLCFGR_PLLP_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSI |
                   (4u << RCC_PLLCFGR_PLLQ_Pos);

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u)
    {
    }

    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
    {
    }

    SystemCoreClock = SPS_SYSTEM_CORE_CLOCK_HZ;
    return true;
}

void system_clock_set_vector_table(unsigned int address)
{
    SCB->VTOR = address;
    __DSB();
    __ISB();
}
