#include "telemetry_status_manager.h"
#include "frame_parser.h"
#include "relay_control_service.h"
#include "ir_control_service.h"
#include "ota_update_manager.h"
#include "projector_service.h"
#include "timebase.h"
#include "uart_ll.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "stm32f4xx.h"
#include <string.h>

typedef struct
{
    sps_system_state_t system_state;
    sps_error_code_t last_error;
    uint8_t projector_state;
    uint8_t ota_state;
    uint32_t reset_reason;
} telemetry_status_t;

static telemetry_status_t telemetry_status;
static StaticSemaphore_t tx_mutex_storage;
static SemaphoreHandle_t tx_mutex;

void telemetry_status_manager_init(void)
{
    memset(&telemetry_status, 0, sizeof(telemetry_status));
    telemetry_status.system_state = SPS_SYSTEM_BOOTING;
    telemetry_status.reset_reason = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;
    tx_mutex = xSemaphoreCreateMutexStatic(&tx_mutex_storage);
    configASSERT(tx_mutex != 0);
}

void telemetry_status_manager_set_system_state(sps_system_state_t state)
{
    telemetry_status.system_state = state;
}

void telemetry_status_manager_set_last_error(sps_error_code_t error)
{
    telemetry_status.last_error = error;
    if (error != SPS_ERR_NONE)
    {
        telemetry_status.system_state = SPS_SYSTEM_ERROR;
    }
}

void telemetry_status_manager_set_projector_state(uint8_t state)
{
    telemetry_status.projector_state = state;
}

void telemetry_status_manager_set_ota_state(uint8_t state)
{
    telemetry_status.ota_state = state;
}

bool telemetry_send_frame(const sps_frame_t *frame)
{
    static uint8_t tx_buffer[SPS_PROTO_MAX_FRAME];
    uint16_t length;
    bool ok;

    if (frame == 0)
    {
        return false;
    }

    if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(200u)) != pdTRUE)
    {
        return false;
    }

    length = frame_builder_build(frame, tx_buffer, sizeof(tx_buffer));
    ok = (length > 0u) && uart_ll_write(UART_LL_PORT_SBC, tx_buffer, length, 250u);
    xSemaphoreGive(tx_mutex);
    return ok;
}

bool telemetry_send_response(uint8_t seq,
                             uint8_t device_id,
                             uint8_t command_id,
                             sps_status_code_t status,
                             sps_error_code_t error,
                             const uint8_t *data,
                             uint16_t data_len)
{
    sps_frame_t frame;

    if (data_len > (SPS_FRAME_MAX_PAYLOAD - 3u))
    {
        return false;
    }

    memset(&frame, 0, sizeof(frame));
    frame.version = SPS_PROTO_VERSION;
    frame.seq = seq;
    frame.msg_type = SPS_MSG_RESPONSE;
    frame.device_id = device_id;
    frame.command_id = command_id;
    frame.payload[0] = (uint8_t)status;
    frame.payload[1] = (uint8_t)(error & 0xFFu);
    frame.payload[2] = (uint8_t)(error >> 8);
    if ((data != 0) && (data_len > 0u))
    {
        memcpy(&frame.payload[3], data, data_len);
    }
    frame.payload_len = (uint16_t)(3u + data_len);

    if (error != SPS_ERR_NONE)
    {
        telemetry_status_manager_set_last_error(error);
    }

    return telemetry_send_frame(&frame);
}

bool telemetry_send_error(uint8_t seq, uint8_t device_id, uint8_t command_id, sps_error_code_t error)
{
    sps_frame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.version = SPS_PROTO_VERSION;
    frame.seq = seq;
    frame.msg_type = SPS_MSG_ERROR;
    frame.device_id = device_id;
    frame.command_id = command_id;
    frame.payload[0] = (uint8_t)(error & 0xFFu);
    frame.payload[1] = (uint8_t)(error >> 8);
    frame.payload_len = 2u;
    telemetry_status_manager_set_last_error(error);
    return telemetry_send_frame(&frame);
}

bool telemetry_send_pong(uint8_t seq)
{
    sps_frame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.version = SPS_PROTO_VERSION;
    frame.seq = seq;
    frame.msg_type = SPS_MSG_PONG;
    frame.device_id = SPS_DEV_SYSTEM;
    frame.command_id = SPS_SYS_PING;
    return telemetry_send_frame(&frame);
}

static void send_periodic_telemetry(void)
{
    sps_frame_t frame;
    uint32_t version = SPS_FIRMWARE_VERSION_U32;
    uint32_t uptime = timebase_millis();
    uint32_t free_heap = 0u;
    uint16_t last_error = (uint16_t)telemetry_status.last_error;
    uint8_t pos = 0u;

    memset(&frame, 0, sizeof(frame));
    frame.version = SPS_PROTO_VERSION;
    frame.seq = 0u;
    frame.msg_type = SPS_MSG_TELEMETRY;
    frame.device_id = SPS_DEV_SYSTEM;
    frame.command_id = SPS_SYS_GET_STATUS;

    frame.payload[pos++] = (uint8_t)(version & 0xFFu);
    frame.payload[pos++] = (uint8_t)(version >> 8);
    frame.payload[pos++] = (uint8_t)(version >> 16);
    frame.payload[pos++] = (uint8_t)(version >> 24);
    frame.payload[pos++] = (uint8_t)(uptime & 0xFFu);
    frame.payload[pos++] = (uint8_t)(uptime >> 8);
    frame.payload[pos++] = (uint8_t)(uptime >> 16);
    frame.payload[pos++] = (uint8_t)(uptime >> 24);
    frame.payload[pos++] = (uint8_t)telemetry_status.system_state;
    frame.payload[pos++] = projector_service_get_state();
    frame.payload[pos++] = relay_control_service_get_light_state();
    frame.payload[pos++] = relay_control_service_get_curtain_state();
    frame.payload[pos++] = relay_control_service_get_screen_state();
    frame.payload[pos++] = ir_control_service_get_power_state();
    frame.payload[pos++] = ir_control_service_get_temperature();
    frame.payload[pos++] = ir_control_service_get_mode();
    frame.payload[pos++] = (uint8_t)(last_error & 0xFFu);
    frame.payload[pos++] = (uint8_t)(last_error >> 8);
    frame.payload[pos++] = ota_update_manager_get_state();
    frame.payload[pos++] = (uint8_t)(telemetry_status.reset_reason & 0xFFu);
    frame.payload[pos++] = (uint8_t)(telemetry_status.reset_reason >> 8);
    frame.payload[pos++] = (uint8_t)(telemetry_status.reset_reason >> 16);
    frame.payload[pos++] = (uint8_t)(telemetry_status.reset_reason >> 24);
    frame.payload[pos++] = (uint8_t)(free_heap & 0xFFu);
    frame.payload[pos++] = (uint8_t)(free_heap >> 8);
    frame.payload[pos++] = (uint8_t)(free_heap >> 16);
    frame.payload[pos++] = (uint8_t)(free_heap >> 24);
    frame.payload[pos++] = (uint8_t)SPS_STATE_SOURCE_ASSUMED;
    frame.payload_len = pos;

    (void)telemetry_send_frame(&frame);
}

void telemetry_task(void *argument)
{
    TickType_t last_wake;
    uint32_t last_telemetry_ms;

    (void)argument;
    last_wake = xTaskGetTickCount();
    last_telemetry_ms = 0u;

    for (;;)
    {
        uint32_t now = timebase_millis();

        relay_control_service_periodic();
        ota_update_manager_confirm_if_stable();
        if ((uint32_t)(now - last_telemetry_ms) >= SPS_TELEMETRY_PERIOD_MS)
        {
            last_telemetry_ms = now;
            send_periodic_telemetry();
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(50u));
    }
}
