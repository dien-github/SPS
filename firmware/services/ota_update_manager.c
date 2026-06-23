#include "ota_update_manager.h"
#include "boot_metadata.h"
#include "crc16_ccitt.h"
#include "crc32.h"
#include "flash_ll.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "sps_config.h"
#include "sps_error.h"
#include "telemetry_status_manager.h"
#include "timebase.h"
#include <stddef.h>
#include <string.h>

typedef struct
{
    bool active;
    sps_slot_id_t target_slot;
    uint32_t version;
    uint32_t expected_size;
    uint32_t expected_crc32;
    uint32_t received_size;
    uint32_t next_block_index;
} ota_session_t;

static StaticQueue_t ota_queue_control;
static uint8_t ota_queue_storage[SPS_OTA_QUEUE_DEPTH * sizeof(sps_frame_t)];
static QueueHandle_t ota_queue;
static ota_session_t ota_session;
static ota_state_t ota_state;

extern uint32_t __app_flash_origin;

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool metadata_read(boot_metadata_t *metadata)
{
    memcpy(metadata, (const void *)SPS_METADATA_BASE, sizeof(*metadata));
    return boot_metadata_validate(metadata);
}

static bool metadata_write(boot_metadata_t *metadata)
{
    metadata->metadata_crc32 = boot_metadata_calculate_crc(metadata);
    if (!flash_ll_unlock())
    {
        return false;
    }
    if (!flash_ll_erase_sector(1u))
    {
        flash_ll_lock();
        return false;
    }
    if (!flash_ll_write(SPS_METADATA_BASE, (const uint8_t *)metadata, sizeof(*metadata)))
    {
        flash_ll_lock();
        return false;
    }
    flash_ll_lock();
    return true;
}

static sps_slot_id_t current_slot(void)
{
    return boot_slot_from_address((uint32_t)&__app_flash_origin);
}

void ota_update_manager_init(void)
{
    ota_queue = xQueueCreateStatic(SPS_OTA_QUEUE_DEPTH, sizeof(sps_frame_t), ota_queue_storage, &ota_queue_control);
    configASSERT(ota_queue != 0);
    memset(&ota_session, 0, sizeof(ota_session));
    ota_state = OTA_STATE_IDLE;
}

bool ota_update_manager_submit(const sps_frame_t *frame)
{
    if ((frame == 0) || (ota_queue == 0))
    {
        return false;
    }
    return xQueueSend(ota_queue, frame, 0u) == pdPASS;
}

uint8_t ota_update_manager_get_state(void)
{
    return (uint8_t)ota_state;
}

static sps_error_code_t ota_start(const sps_frame_t *frame)
{
    sps_slot_id_t slot;
    uint32_t slot_size;

    if (frame->payload_len != 13u)
    {
        return SPS_ERR_BAD_LENGTH;
    }

    slot = (sps_slot_id_t)frame->payload[12];
    slot_size = boot_slot_size(slot);
    if ((slot_size == 0u) || (slot == current_slot()))
    {
        return SPS_ERR_INVALID_PAYLOAD;
    }

    ota_session.version = read_le32(&frame->payload[0]);
    ota_session.expected_size = read_le32(&frame->payload[4]);
    ota_session.expected_crc32 = read_le32(&frame->payload[8]);
    ota_session.target_slot = slot;
    ota_session.received_size = 0u;
    ota_session.next_block_index = 0u;

    if ((ota_session.expected_size == 0u) || (ota_session.expected_size > slot_size))
    {
        return SPS_ERR_BAD_LENGTH;
    }

    ota_state = OTA_STATE_ERASING;
    telemetry_status_manager_set_ota_state((uint8_t)ota_state);

    if (!flash_ll_unlock())
    {
        ota_state = OTA_STATE_ERROR;
        return SPS_ERR_FLASH_WRITE_FAIL;
    }
    if (!flash_ll_erase_range(boot_slot_base(slot), slot_size))
    {
        flash_ll_lock();
        ota_state = OTA_STATE_ERROR;
        return SPS_ERR_FLASH_WRITE_FAIL;
    }
    flash_ll_lock();

    ota_session.active = true;
    ota_state = OTA_STATE_RECEIVING;
    telemetry_status_manager_set_system_state(SPS_SYSTEM_OTA);
    telemetry_status_manager_set_ota_state((uint8_t)ota_state);
    return SPS_ERR_NONE;
}

static sps_error_code_t ota_block(const sps_frame_t *frame)
{
    uint32_t offset;
    uint32_t block_index;
    uint16_t rx_crc;
    uint16_t calc_crc;
    uint16_t data_len;
    uint32_t address;

    if ((!ota_session.active) || (frame->payload_len < 10u))
    {
        return SPS_ERR_OTA_INVALID_BLOCK;
    }

    data_len = (uint16_t)(frame->payload_len - 10u);
    if ((data_len == 0u) || (data_len > SPS_OTA_BLOCK_MAX_DATA))
    {
        return SPS_ERR_OTA_INVALID_BLOCK;
    }

    offset = read_le32(&frame->payload[0]);
    block_index = read_le32(&frame->payload[4]);
    rx_crc = (uint16_t)frame->payload[8] | ((uint16_t)frame->payload[9] << 8);
    calc_crc = crc16_ccitt_false(&frame->payload[10], data_len);

    if ((rx_crc != calc_crc) || (offset != ota_session.received_size) || (block_index != ota_session.next_block_index))
    {
        return SPS_ERR_OTA_INVALID_BLOCK;
    }

    if ((offset + data_len) > ota_session.expected_size)
    {
        return SPS_ERR_OTA_INVALID_BLOCK;
    }

    address = boot_slot_base(ota_session.target_slot) + offset;
    if (!flash_ll_unlock())
    {
        return SPS_ERR_FLASH_WRITE_FAIL;
    }
    if (!flash_ll_write(address, &frame->payload[10], data_len))
    {
        flash_ll_lock();
        return SPS_ERR_FLASH_WRITE_FAIL;
    }
    flash_ll_lock();

    ota_session.received_size += data_len;
    ota_session.next_block_index++;
    return SPS_ERR_NONE;
}

static sps_error_code_t ota_end(void)
{
    boot_metadata_t metadata;
    uint32_t crc;
    sps_slot_id_t current;

    if ((!ota_session.active) || (ota_session.received_size != ota_session.expected_size))
    {
        ota_state = OTA_STATE_ERROR;
        return SPS_ERR_OTA_VERIFY_FAIL;
    }

    ota_state = OTA_STATE_VERIFYING;
    telemetry_status_manager_set_ota_state((uint8_t)ota_state);
    crc = crc32_ieee((const uint8_t *)boot_slot_base(ota_session.target_slot), ota_session.expected_size);
    if (crc != ota_session.expected_crc32)
    {
        ota_state = OTA_STATE_ERROR;
        return SPS_ERR_OTA_VERIFY_FAIL;
    }

    if (!metadata_read(&metadata))
    {
        boot_metadata_defaults(&metadata);
        current = current_slot();
        metadata.active_slot = current;
        metadata.confirmed_slot = current;
        metadata.rollback_slot = current;
        metadata.slots[current].magic = BOOT_SLOT_MAGIC;
        metadata.slots[current].vector_address = boot_slot_base(current);
        metadata.slots[current].flags = BOOT_FLAG_CONFIRMED;
    }

    metadata.pending_slot = ota_session.target_slot;
    metadata.rollback_slot = current_slot();
    metadata.boot_attempts = 0u;
    metadata.slots[ota_session.target_slot].magic = BOOT_SLOT_MAGIC;
    metadata.slots[ota_session.target_slot].version = ota_session.version;
    metadata.slots[ota_session.target_slot].image_size = ota_session.expected_size;
    metadata.slots[ota_session.target_slot].image_crc32 = ota_session.expected_crc32;
    metadata.slots[ota_session.target_slot].vector_address = boot_slot_base(ota_session.target_slot);
    metadata.slots[ota_session.target_slot].flags = BOOT_FLAG_PENDING;
    metadata.last_boot_result = BOOT_RESULT_NONE;

    if (!metadata_write(&metadata))
    {
        ota_state = OTA_STATE_ERROR;
        return SPS_ERR_FLASH_WRITE_FAIL;
    }

    ota_session.active = false;
    ota_state = OTA_STATE_READY_TO_REBOOT;
    telemetry_status_manager_set_ota_state((uint8_t)ota_state);
    return SPS_ERR_NONE;
}

static void ota_abort(void)
{
    memset(&ota_session, 0, sizeof(ota_session));
    ota_state = OTA_STATE_IDLE;
    telemetry_status_manager_set_ota_state((uint8_t)ota_state);
    telemetry_status_manager_set_system_state(SPS_SYSTEM_READY);
}

void ota_update_manager_task(void *argument)
{
    sps_frame_t frame;

    (void)argument;
    for (;;)
    {
        if (xQueueReceive(ota_queue, &frame, portMAX_DELAY) == pdTRUE)
        {
            sps_error_code_t error = SPS_ERR_UNKNOWN_COMMAND;
            uint8_t progress[8];
            uint16_t progress_len = 0u;

            if (frame.msg_type == SPS_MSG_OTA_START)
            {
                error = ota_start(&frame);
            }
            else if (frame.msg_type == SPS_MSG_OTA_BLOCK)
            {
                error = ota_block(&frame);
                progress[0] = (uint8_t)(ota_session.received_size & 0xFFu);
                progress[1] = (uint8_t)(ota_session.received_size >> 8);
                progress[2] = (uint8_t)(ota_session.received_size >> 16);
                progress[3] = (uint8_t)(ota_session.received_size >> 24);
                progress[4] = (uint8_t)(ota_session.next_block_index & 0xFFu);
                progress[5] = (uint8_t)(ota_session.next_block_index >> 8);
                progress[6] = (uint8_t)(ota_session.next_block_index >> 16);
                progress[7] = (uint8_t)(ota_session.next_block_index >> 24);
                progress_len = sizeof(progress);
            }
            else if (frame.msg_type == SPS_MSG_OTA_END)
            {
                error = ota_end();
            }
            else if (frame.msg_type == SPS_MSG_OTA_ABORT)
            {
                ota_abort();
                error = SPS_ERR_NONE;
            }

            (void)telemetry_send_response(frame.seq,
                                          SPS_DEV_OTA,
                                          frame.command_id,
                                          (error == SPS_ERR_NONE) ? ((frame.msg_type == SPS_MSG_OTA_END) ? SPS_STATUS_ACCEPTED : SPS_STATUS_OK) : SPS_STATUS_FAIL,
                                          error,
                                          progress_len > 0u ? progress : 0,
                                          progress_len);
        }
    }
}

void ota_update_manager_confirm_if_stable(void)
{
    static bool confirmed_this_boot;
    boot_metadata_t metadata;
    sps_slot_id_t slot;

    if (confirmed_this_boot || (timebase_millis() < SPS_BOOT_CONFIRM_STABLE_MS))
    {
        return;
    }

    slot = current_slot();
    if (metadata_read(&metadata) &&
        (metadata.pending_slot == slot) &&
        (metadata.active_slot == slot))
    {
        metadata.pending_slot = BOOT_SLOT_INVALID;
        metadata.confirmed_slot = slot;
        metadata.rollback_slot = slot;
        metadata.boot_attempts = 0u;
        metadata.slots[slot].flags &= ~BOOT_FLAG_PENDING;
        metadata.slots[slot].flags |= BOOT_FLAG_CONFIRMED;
        metadata.last_boot_result = BOOT_RESULT_SUCCESS;
        (void)metadata_write(&metadata);
    }

    confirmed_this_boot = true;
}
