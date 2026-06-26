#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool boot_flash_unlock(void);
void boot_flash_lock(void);
bool boot_flash_erase_sector(uint8_t sector);
bool boot_flash_write(uint32_t address, const uint8_t *data, size_t length);

#endif
