#ifndef CRC16_CCITT_H
#define CRC16_CCITT_H

#include <stddef.h>
#include <stdint.h>

uint16_t crc16_ccitt_false(const uint8_t *data, size_t length);
uint16_t crc16_ccitt_update(uint16_t crc, const uint8_t *data, size_t length);

#endif
