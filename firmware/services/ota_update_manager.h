#ifndef OTA_UPDATE_MANAGER_H
#define OTA_UPDATE_MANAGER_H

#include "sps_protocol.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    OTA_STATE_IDLE = 0,
    OTA_STATE_ERASING = 1,
    OTA_STATE_RECEIVING = 2,
    OTA_STATE_VERIFYING = 3,
    OTA_STATE_READY_TO_REBOOT = 4,
    OTA_STATE_ERROR = 5
} ota_state_t;

void ota_update_manager_init(void);
bool ota_update_manager_submit(const sps_frame_t *frame);
void ota_update_manager_task(void *argument);
uint8_t ota_update_manager_get_state(void);
void ota_update_manager_confirm_if_stable(void);

#endif
