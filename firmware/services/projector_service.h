#ifndef PROJECTOR_SERVICE_H
#define PROJECTOR_SERVICE_H

#include "sps_error.h"
#include "sps_protocol.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    PROJECTOR_STATE_UNKNOWN = 0,
    PROJECTOR_STATE_READY = 1,
    PROJECTOR_STATE_BUSY = 2,
    PROJECTOR_STATE_ERROR = 3
} projector_state_t;

void projector_service_init(void);
bool projector_service_submit(const sps_frame_t *frame);
void projector_service_task(void *argument);
uint8_t projector_service_get_state(void);

#endif
