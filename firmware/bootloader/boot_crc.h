#ifndef BOOT_CRC_H
#define BOOT_CRC_H

#include <stdint.h>

uint32_t boot_crc32_image(uint32_t address, uint32_t size);

#endif
