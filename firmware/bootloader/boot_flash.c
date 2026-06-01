#include "boot_flash.h"
#include "stm32f4xx.h"

#define BOOT_FLASH_KEY1 0x45670123u
#define BOOT_FLASH_KEY2 0xCDEF89ABu

static bool wait_ready(void)
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

bool boot_flash_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) == 0u)
    {
        return true;
    }
    FLASH->KEYR = BOOT_FLASH_KEY1;
    FLASH->KEYR = BOOT_FLASH_KEY2;
    return (FLASH->CR & FLASH_CR_LOCK) == 0u;
}

void boot_flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

bool boot_flash_erase_sector(uint8_t sector)
{
    if (!wait_ready())
    {
        return false;
    }

    FLASH->SR = FLASH_SR_EOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;
    FLASH->CR &= ~(FLASH_CR_SNB | FLASH_CR_PSIZE);
    FLASH->CR |= FLASH_CR_SER | ((uint32_t)sector << FLASH_CR_SNB_Pos) | FLASH_CR_PSIZE_1;
    FLASH->CR |= FLASH_CR_STRT;
    if (!wait_ready())
    {
        FLASH->CR &= ~FLASH_CR_SER;
        return false;
    }
    FLASH->CR &= ~FLASH_CR_SER;
    return true;
}

bool boot_flash_write(uint32_t address, const uint8_t *data, size_t length)
{
    if ((data == 0) && (length > 0u))
    {
        return false;
    }

    for (size_t i = 0u; i < length; i++)
    {
        if (!wait_ready())
        {
            FLASH->CR &= ~FLASH_CR_PG;
            return false;
        }
        FLASH->CR &= ~FLASH_CR_PSIZE;
        FLASH->CR |= FLASH_CR_PG;
        *(volatile uint8_t *)(address + i) = data[i];
    }
    if (!wait_ready())
    {
        FLASH->CR &= ~FLASH_CR_PG;
        return false;
    }
    FLASH->CR &= ~FLASH_CR_PG;
    return true;
}
