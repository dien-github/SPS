#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdint.h>

void boot_jump_to_application(uint32_t vector_address);

#endif
