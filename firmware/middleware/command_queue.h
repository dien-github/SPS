#ifndef COMMAND_QUEUE_H
#define COMMAND_QUEUE_H

#include "sps_protocol.h"
#include <stdbool.h>

typedef struct
{
    sps_frame_t frame;
} sps_command_t;

void command_queue_init(void);
bool command_queue_push(const sps_frame_t *frame);
bool command_queue_pop(sps_command_t *command, uint32_t timeout_ms);

#endif
