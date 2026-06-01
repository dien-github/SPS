#include "boot_jump.h"
#include "stm32f4xx.h"

typedef void (*app_entry_t)(void);

void boot_jump_to_application(uint32_t vector_address)
{
    uint32_t initial_sp = *(const volatile uint32_t *)vector_address;
    uint32_t reset = *(const volatile uint32_t *)(vector_address + 4u);
    app_entry_t app_reset = (app_entry_t)reset;

    __disable_irq();
    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;
    SCB->VTOR = vector_address;
    __set_MSP(initial_sp);
    __DSB();
    __ISB();
    app_reset();
}
