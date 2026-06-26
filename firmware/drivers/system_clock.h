#ifndef SYSTEM_CLOCK_H
#define SYSTEM_CLOCK_H

#include <stdbool.h>

bool system_clock_config_84mhz_hsi(void);
void system_clock_set_vector_table(unsigned int address);

#endif
