#include "flash_ll.h"
#include "stm32f4xx.h"

#define FLASH_KEY1 0x45670123u
#define FLASH_KEY2 0xCDEF89ABu

static bool flash_wait_ready(void)
{
    while ((FLASH->SR & FLASH_SR_BSY) != 0u)
    {
    }

    if ((FLASH->SR & (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)) != 0u)
    {
        FLASH->SR = FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;
        return false;
    }

    return true;
}

bool flash_ll_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) == 0u)
    {
        return true;
    }

    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
    return (FLASH->CR & FLASH_CR_LOCK) == 0u;
}

void flash_ll_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

int flash_ll_sector_from_address(uint32_t address)
{
    if ((address >= 0x08000000u) && (address < 0x08004000u))
    {
        return 0;
    }
    if ((address >= 0x08004000u) && (address < 0x08008000u))
    {
        return 1;
    }
    if ((address >= 0x08008000u) && (address < 0x0800C000u))
    {
        return 2;
    }
    if ((address >= 0x0800C000u) && (address < 0x08010000u))
    {
        return 3;
    }
    if ((address >= 0x08010000u) && (address < 0x08020000u))
    {
        return 4;
    }
    if ((address >= 0x08020000u) && (address < 0x08040000u))
    {
        return 5;
    }

    return -1;
}

bool flash_ll_erase_sector(uint8_t sector)
{
    if (sector > 5u)
    {
        return false;
    }

    if (!flash_wait_ready())
    {
        return false;
    }

    FLASH->SR = FLASH_SR_EOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;
    FLASH->CR &= ~(FLASH_CR_SNB | FLASH_CR_PSIZE);
    FLASH->CR |= FLASH_CR_SER | ((uint32_t)sector << FLASH_CR_SNB_Pos) | FLASH_CR_PSIZE_1;
    FLASH->CR |= FLASH_CR_STRT;
    if (!flash_wait_ready())
    {
        FLASH->CR &= ~FLASH_CR_SER;
        return false;
    }

    FLASH->CR &= ~FLASH_CR_SER;
    return true;
}

bool flash_ll_erase_range(uint32_t address, uint32_t length)
{
    uint32_t end;
    int first;
    int last;

    if (length == 0u)
    {
        return false;
    }

    end = address + length - 1u;
    first = flash_ll_sector_from_address(address);
    last = flash_ll_sector_from_address(end);
    if ((first < 0) || (last < 0))
    {
        return false;
    }

    for (int sector = first; sector <= last; sector++)
    {
        if (!flash_ll_erase_sector((uint8_t)sector))
        {
            return false;
        }
    }

    return true;
}

bool flash_ll_write(uint32_t address, const uint8_t *data, size_t length)
{
    if ((data == 0) && (length > 0u))
    {
        return false;
    }

    for (size_t i = 0u; i < length; i++)
    {
        if (!flash_wait_ready())
        {
            FLASH->CR &= ~FLASH_CR_PG;
            return false;
        }

        FLASH->CR &= ~FLASH_CR_PSIZE;
        FLASH->CR |= FLASH_CR_PG;
        *(volatile uint8_t *)(address + i) = data[i];
    }

    if (!flash_wait_ready())
    {
        FLASH->CR &= ~FLASH_CR_PG;
        return false;
    }

    FLASH->CR &= ~FLASH_CR_PG;
    for (size_t i = 0u; i < length; i++)
    {
        if (*(const volatile uint8_t *)(address + i) != data[i])
        {
            return false;
        }
    }

    return true;
}

bool flash_ll_is_range_erased(uint32_t address, uint32_t length)
{
    for (uint32_t i = 0u; i < length; i++)
    {
        if (*(const volatile uint8_t *)(address + i) != 0xFFu)
        {
            return false;
        }
    }

    return true;
}
