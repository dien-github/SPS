#ifndef GPIO_RELAY_LL_H
#define GPIO_RELAY_LL_H

#include <stdbool.h>
#include <stdint.h>

void gpio_relay_ll_init(void);
void gpio_relay_ll_all_off(void);
bool gpio_relay_ll_set(uint8_t channel, bool on);
bool gpio_relay_ll_get(uint8_t channel);

#endif
