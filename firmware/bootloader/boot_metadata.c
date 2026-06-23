#include "boot_metadata.h"
#include "boot_flash.h"
#include "crc32.h"
#include <stddef.h>
#include <string.h>

void boot_metadata_defaults(boot_metadata_t *metadata)
{
    if (metadata == 0)
    {
        return;
    }

    memset(metadata, 0xFF, sizeof(*metadata));
    metadata->magic = BOOT_METADATA_MAGIC;
    metadata->struct_version = BOOT_METADATA_VERSION;
    metadata->active_slot = BOOT_SLOT_INVALID;
    metadata->confirmed_slot = BOOT_SLOT_INVALID;
    metadata->pending_slot = BOOT_SLOT_INVALID;
    metadata->rollback_slot = BOOT_SLOT_INVALID;
    metadata->boot_attempts = 0u;
    metadata->last_boot_result = BOOT_RESULT_NONE;
}

uint32_t boot_metadata_calculate_crc(const boot_metadata_t *metadata)
{
    boot_metadata_t temp;

    if (metadata == 0)
    {
        return 0u;
    }

    temp = *metadata;
    temp.metadata_crc32 = 0u;
    return crc32_ieee((const uint8_t *)&temp, offsetof(boot_metadata_t, metadata_crc32));
}

bool boot_metadata_validate(const boot_metadata_t *metadata)
{
    if ((metadata == 0) ||
        (metadata->magic != BOOT_METADATA_MAGIC) ||
        (metadata->struct_version != BOOT_METADATA_VERSION))
    {
        return false;
    }

    return boot_metadata_calculate_crc(metadata) == metadata->metadata_crc32;
}

uint32_t boot_slot_base(sps_slot_id_t slot)
{
    if (slot == SPS_SLOT_A)
    {
        return SPS_APP_SLOT_A_BASE;
    }
    if (slot == SPS_SLOT_B)
    {
        return SPS_APP_SLOT_B_BASE;
    }
    return 0u;
}

uint32_t boot_slot_size(sps_slot_id_t slot)
{
    if (slot == SPS_SLOT_A)
    {
        return SPS_APP_SLOT_A_SIZE;
    }
    if (slot == SPS_SLOT_B)
    {
        return SPS_APP_SLOT_B_SIZE;
    }
    return 0u;
}

sps_slot_id_t boot_slot_from_address(uint32_t address)
{
    if ((address >= SPS_APP_SLOT_A_BASE) && (address < (SPS_APP_SLOT_A_BASE + SPS_APP_SLOT_A_SIZE)))
    {
        return SPS_SLOT_A;
    }
    if ((address >= SPS_APP_SLOT_B_BASE) && (address < (SPS_APP_SLOT_B_BASE + SPS_APP_SLOT_B_SIZE)))
    {
        return SPS_SLOT_B;
    }
    return SPS_SLOT_INVALID;
}

bool boot_slot_vector_is_plausible(uint32_t vector_address)
{
    uint32_t initial_sp = *(const volatile uint32_t *)vector_address;
    uint32_t reset = *(const volatile uint32_t *)(vector_address + 4u);

    if ((initial_sp < 0x20000000u) || (initial_sp > 0x20010000u))
    {
        return false;
    }

    return (reset >= SPS_APP_SLOT_A_BASE) && (reset < SPS_FLASH_END) && ((reset & 1u) == 1u);
}

bool boot_metadata_read(boot_metadata_t *metadata)
{
    memcpy(metadata, (const void *)SPS_METADATA_BASE, sizeof(*metadata));
    return boot_metadata_validate(metadata);
}

bool boot_metadata_write(boot_metadata_t *metadata)
{
    metadata->metadata_crc32 = boot_metadata_calculate_crc(metadata);
    if (!boot_flash_unlock())
    {
        return false;
    }
    if (!boot_flash_erase_sector(1u))
    {
        boot_flash_lock();
        return false;
    }
    if (!boot_flash_write(SPS_METADATA_BASE, (const uint8_t *)metadata, sizeof(*metadata)))
    {
        boot_flash_lock();
        return false;
    }
    boot_flash_lock();
    return true;
}
