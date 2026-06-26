#include "boot_crc.h"
#include "crc32.h"

uint32_t boot_crc32_image(uint32_t address, uint32_t size)
{
    return crc32_ieee((const uint8_t *)address, size);
}
