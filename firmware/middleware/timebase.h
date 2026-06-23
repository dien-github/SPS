#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdint.h>

void timebase_init(void);
void timebase_tick_isr(void);
uint32_t timebase_millis(void);
void timebase_delay_ms(uint32_t ms);

#endif
