#ifndef TELEMETRY_STATUS_MANAGER_H
#define TELEMETRY_STATUS_MANAGER_H

#include "sps_error.h"
#include "sps_protocol.h"
#include "sps_types.h"
#include <stdbool.h>
#include <stdint.h>

void telemetry_status_manager_init(void);
void telemetry_status_manager_set_system_state(sps_system_state_t state);
void telemetry_status_manager_set_last_error(sps_error_code_t error);
void telemetry_status_manager_set_projector_state(uint8_t state);
void telemetry_status_manager_set_ota_state(uint8_t state);
bool telemetry_send_frame(const sps_frame_t *frame);
bool telemetry_send_response(uint8_t seq,
                             uint8_t device_id,
                             uint8_t command_id,
                             sps_status_code_t status,
                             sps_error_code_t error,
                             const uint8_t *data,
                             uint16_t data_len);
bool telemetry_send_error(uint8_t seq, uint8_t device_id, uint8_t command_id, sps_error_code_t error);
bool telemetry_send_pong(uint8_t seq);
void telemetry_task(void *argument);

#endif
