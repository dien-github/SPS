#ifndef WATCHDOG_LL_H
#define WATCHDOG_LL_H

#include <stdint.h>

void watchdog_ll_init(uint32_t timeout_ms);
void watchdog_ll_feed(void);

#endif
