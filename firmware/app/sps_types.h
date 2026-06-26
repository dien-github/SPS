#ifndef SPS_TYPES_H
#define SPS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    SPS_SLOT_A = 0,
    SPS_SLOT_B = 1,
    SPS_SLOT_INVALID = 0xFF
} sps_slot_id_t;

typedef enum
{
    SPS_SYSTEM_BOOTING = 0,
    SPS_SYSTEM_READY,
    SPS_SYSTEM_BUSY,
    SPS_SYSTEM_OTA,
    SPS_SYSTEM_ERROR
} sps_system_state_t;

typedef enum
{
    SPS_STATE_SOURCE_ASSUMED = 0,
    SPS_STATE_SOURCE_PHYSICAL = 1
} sps_state_source_t;

#endif
