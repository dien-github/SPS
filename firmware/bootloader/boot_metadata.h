#ifndef BOOT_METADATA_H
#define BOOT_METADATA_H

#include "sps_config.h"
#include "sps_types.h"
#include <stdbool.h>
#include <stdint.h>

#define BOOT_METADATA_MAGIC        0x5350534Du
#define BOOT_METADATA_VERSION      1u
#define BOOT_SLOT_MAGIC            0x53505349u
#define BOOT_SLOT_INVALID          0xFFFFFFFFu
#define BOOT_FLAG_CONFIRMED        0x00000001u
#define BOOT_FLAG_PENDING          0x00000002u
#define BOOT_RESULT_NONE           0u
#define BOOT_RESULT_SUCCESS        1u
#define BOOT_RESULT_ROLLBACK       2u
#define BOOT_RESULT_VERIFY_FAIL    3u

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t vector_address;
    uint32_t flags;
    uint32_t boot_count;
} boot_slot_metadata_t;

typedef struct
{
    uint32_t magic;
    uint32_t struct_version;
    uint32_t active_slot;
    uint32_t confirmed_slot;
    uint32_t pending_slot;
    uint32_t rollback_slot;
    uint32_t boot_attempts;
    boot_slot_metadata_t slots[2];
    uint32_t last_boot_result;
    uint32_t metadata_crc32;
} boot_metadata_t;

void boot_metadata_defaults(boot_metadata_t *metadata);
bool boot_metadata_validate(const boot_metadata_t *metadata);
uint32_t boot_metadata_calculate_crc(const boot_metadata_t *metadata);
bool boot_metadata_read(boot_metadata_t *metadata);
bool boot_metadata_write(boot_metadata_t *metadata);
uint32_t boot_slot_base(sps_slot_id_t slot);
uint32_t boot_slot_size(sps_slot_id_t slot);
sps_slot_id_t boot_slot_from_address(uint32_t address);
bool boot_slot_vector_is_plausible(uint32_t vector_address);

#endif
