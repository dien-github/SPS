#include "boot_crc.h"
#include "boot_jump.h"
#include "boot_metadata.h"
#include "system_clock.h"
#include <stdbool.h>

bool boot_metadata_read(boot_metadata_t *metadata);
bool boot_metadata_write(boot_metadata_t *metadata);

static bool slot_image_valid(const boot_metadata_t *metadata, sps_slot_id_t slot)
{
    const boot_slot_metadata_t *slot_meta;

    if ((metadata == 0) || (slot > SPS_SLOT_B))
    {
        return false;
    }

    slot_meta = &metadata->slots[slot];
    if ((slot_meta->magic != BOOT_SLOT_MAGIC) ||
        (slot_meta->image_size == 0u) ||
        (slot_meta->image_size > boot_slot_size(slot)) ||
        (slot_meta->vector_address != boot_slot_base(slot)) ||
        !boot_slot_vector_is_plausible(slot_meta->vector_address))
    {
        return false;
    }

    return boot_crc32_image(slot_meta->vector_address, slot_meta->image_size) == slot_meta->image_crc32;
}

static sps_slot_id_t choose_slot(boot_metadata_t *metadata)
{
    if (!boot_metadata_validate(metadata))
    {
        if (boot_slot_vector_is_plausible(SPS_APP_SLOT_A_BASE))
        {
            return SPS_SLOT_A;
        }
        if (boot_slot_vector_is_plausible(SPS_APP_SLOT_B_BASE))
        {
            return SPS_SLOT_B;
        }
        return SPS_SLOT_INVALID;
    }

    if ((metadata->pending_slot <= SPS_SLOT_B) && ((metadata->slots[metadata->pending_slot].flags & BOOT_FLAG_PENDING) != 0u))
    {
        if (slot_image_valid(metadata, (sps_slot_id_t)metadata->pending_slot) &&
            (metadata->boot_attempts < SPS_BOOT_PENDING_MAX_ATTEMPTS))
        {
            metadata->active_slot = metadata->pending_slot;
            metadata->boot_attempts++;
            metadata->slots[metadata->pending_slot].boot_count++;
            (void)boot_metadata_write(metadata);
            return (sps_slot_id_t)metadata->pending_slot;
        }

        metadata->pending_slot = BOOT_SLOT_INVALID;
        metadata->active_slot = metadata->rollback_slot;
        metadata->last_boot_result = BOOT_RESULT_ROLLBACK;
        (void)boot_metadata_write(metadata);
    }

    if ((metadata->confirmed_slot <= SPS_SLOT_B) && slot_image_valid(metadata, (sps_slot_id_t)metadata->confirmed_slot))
    {
        metadata->active_slot = metadata->confirmed_slot;
        metadata->boot_attempts = 0u;
        (void)boot_metadata_write(metadata);
        return (sps_slot_id_t)metadata->confirmed_slot;
    }

    if ((metadata->rollback_slot <= SPS_SLOT_B) && slot_image_valid(metadata, (sps_slot_id_t)metadata->rollback_slot))
    {
        metadata->active_slot = metadata->rollback_slot;
        metadata->confirmed_slot = metadata->rollback_slot;
        metadata->pending_slot = BOOT_SLOT_INVALID;
        metadata->boot_attempts = 0u;
        metadata->last_boot_result = BOOT_RESULT_ROLLBACK;
        (void)boot_metadata_write(metadata);
        return (sps_slot_id_t)metadata->rollback_slot;
    }

    if (boot_slot_vector_is_plausible(SPS_APP_SLOT_A_BASE))
    {
        return SPS_SLOT_A;
    }

    return SPS_SLOT_INVALID;
}

int main(void)
{
    boot_metadata_t metadata;
    sps_slot_id_t slot;

    (void)system_clock_config_84mhz_hsi();
    if (!boot_metadata_read(&metadata))
    {
        boot_metadata_defaults(&metadata);
    }

    slot = choose_slot(&metadata);
    if (slot != SPS_SLOT_INVALID)
    {
        boot_jump_to_application(boot_slot_base(slot));
    }

    for (;;)
    {
        __asm volatile ("wfi");
    }
}
