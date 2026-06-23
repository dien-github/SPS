#include "crc32.h"

uint32_t crc32_ieee_init(void)
{
    return 0xFFFFFFFFu;
}

uint32_t crc32_ieee_update(uint32_t crc, const uint8_t *data, size_t length)
{
    if ((data == 0) && (length > 0u))
    {
        return 0u;
    }

    for (size_t i = 0u; i < length; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0u; bit < 8u; bit++)
        {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }

    return crc;
}

uint32_t crc32_ieee_finish(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

uint32_t crc32_ieee(const uint8_t *data, size_t length)
{
    return crc32_ieee_finish(crc32_ieee_update(crc32_ieee_init(), data, length));
}
