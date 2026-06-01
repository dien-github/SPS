#ifndef TIMER_LL_H
#define TIMER_LL_H

#include <stdint.h>

void timer_ll_init_cycle_counter(void);
void timer_ll_delay_us(uint32_t us);
uint32_t timer_ll_cycles(void);

#endif
