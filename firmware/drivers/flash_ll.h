#ifndef FLASH_LL_H
#define FLASH_LL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool flash_ll_unlock(void);
void flash_ll_lock(void);
bool flash_ll_erase_sector(uint8_t sector);
bool flash_ll_erase_range(uint32_t address, uint32_t length);
bool flash_ll_write(uint32_t address, const uint8_t *data, size_t length);
bool flash_ll_is_range_erased(uint32_t address, uint32_t length);
int flash_ll_sector_from_address(uint32_t address);

#endif
