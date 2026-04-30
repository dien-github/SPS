#include "command_dispatcher.h"
#include "relay_service.h"
#include "cmsis_os.h"

extern osMessageQueueId_t relayQueueHandle;

static void Command_DispatchLight(const sps_frame_t *frame)
{
    uint8_t light_id;
    uint8_t state;
    uint32_t relay_cmd;
    osStatus_t queue_status;

    if (frame->length != 2U)
    {
        SPS_Protocol_SendNack(frame->cmd_id, SPS_ERR_BAD_LENGTH);
        return;
    }

    light_id = frame->payload[0];
    state = frame->payload[1];

    /*
     * Tai phan cung hien tai chi co 1 relay den phong.
     * Chap nhan:
     *   0x01: den buc
     *   0x02: den lop
     *   0xFF: tat ca
     */
    if (!((light_id == 0x01U) || (light_id == 0x02U) || (light_id == 0xFFU)))
    {
        SPS_Protocol_SendNack(frame->cmd_id, SPS_ERR_INVALID_PARAM);
        return;
    }

    if (!((state == 0x00U) || (state == 0x01U)))
    {
        SPS_Protocol_SendNack(frame->cmd_id, SPS_ERR_INVALID_PARAM);
        return;
    }

    relay_cmd = RELAY_CMD_PACK(RELAY_DEV_LIGHT, state, 0U, frame->cmd_id);

    queue_status = osMessageQueuePut(relayQueueHandle, &relay_cmd, 0U, 0U);

    if (queue_status == osOK)
    {
        SPS_Protocol_SendAck(frame->cmd_id);
    }
    else
    {
        SPS_Protocol_SendNack(frame->cmd_id, SPS_ERR_QUEUE_FULL);
    }
}

static void Command_DispatchRelayMotor(const sps_frame_t *frame)
{
    uint8_t device_id;
    uint8_t action;
    uint8_t relay_device;
    uint32_t relay_cmd;
    osStatus_t queue_status;

    if (frame->length != 2U)
    {
        SPS_Protocol_SendNack(frame->cmd_id, SPS_ERR_BAD_LENGTH);
        return;
    }

    device_id = frame->payload[0];
    action = frame->payload[1];

    /*
     * Theo protocol:
     *   payload[0] = 0x01: Rem cua
     *   payload[0] = 0x02: Man chieu
     */
    if (device_id == 0x01U)
    {
        relay_device = RELAY_DEV_CURTAIN;
    }
    else if (device_id == 0x02U)
    {
        relay_device = RELAY_DEV_SCREEN;
    }
    else
    {
        SPS_Protocol_SendNack(frame->cmd_id, SPS_ERR_INVALID_PARAM);
        return;
    }

    /*
     * Theo protocol:
     *   0x01: Mo / Len
     *   0x00: Dong / Xuong
     *   0x02: Dung
     */
    if (!((action == 0x00U) || (action == 0x01U) || (action == 0x02U)))
    {
        SPS_Protocol_SendNack(frame->cmd_id, SPS_ERR_INVALID_PARAM);
        return;
    }

    relay_cmd = RELAY_CMD_PACK(relay_device, action, 0U, frame->cmd_id);

    queue_status = osMessageQueuePut(relayQueueHandle, &relay_cmd, 0U, 0U);

    if (queue_status == osOK)
    {
        SPS_Protocol_SendAck(frame->cmd_id);
    }
    else
    {
        SPS_Protocol_SendNack(frame->cmd_id, SPS_ERR_QUEUE_FULL);
    }
}

static void Command_DispatchRelayStatus(const sps_frame_t *frame)
{
    uint8_t query_id;
    uint8_t relay_device;
    uint8_t status_payload[2];

    if (frame->length != 1U)
    {
        SPS_Protocol_SendNack(frame->cmd_id, SPS_ERR_BAD_LENGTH);
        return;
    }

    query_id = frame->payload[0];

    /*
     * Ho tro ca 2 cach goi:
     *   0x01 hoac 0x21: den
     *   0x02: rem
     *   0x03: man chieu
     */
    if ((query_id == 0x01U) || (query_id == SPS_CMD_LIGHT_CONTROL))
    {
        relay_device = RELAY_DEV_LIGHT;
    }
    else if (query_id == 0x02U)
    {
        relay_device = RELAY_DEV_CURTAIN;
    }
    else if (query_id == 0x03U)
    {
        relay_device = RELAY_DEV_SCREEN;
    }
    else
    {
        SPS_Protocol_SendNack(frame->cmd_id, SPS_ERR_INVALID_PARAM);
        return;
    }

    status_payload[0] = query_id;
    status_payload[1] = Relay_Service_GetStatus(relay_device);

    SPS_Protocol_SendFrame(SPS_CMD_RELAY_STATUS, status_payload, 2U);
}

void Command_DispatchFrame(const sps_frame_t *frame)
{
    switch (frame->cmd_id)
    {
        case SPS_CMD_PING:
        {
            if (frame->length == 0U)
            {
                SPS_Protocol_SendAck(SPS_CMD_PING);
            }
            else
            {
                SPS_Protocol_SendNack(frame->cmd_id, SPS_ERR_BAD_LENGTH);
            }
            break;
        }

        case SPS_CMD_LIGHT_CONTROL:
        {
            Command_DispatchLight(frame);
            break;
        }

        case SPS_CMD_RELAY_CONTROL:
        {
            Command_DispatchRelayMotor(frame);
            break;
        }

        case SPS_CMD_RELAY_STATUS:
        {
            Command_DispatchRelayStatus(frame);
            break;
        }

        default:
        {
            SPS_Protocol_SendNack(frame->cmd_id, SPS_ERR_UNKNOWN_CMD);
            break;
        }
    }
}
