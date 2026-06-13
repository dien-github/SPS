#ifndef COMMAND_DISPATCHER_H
#define COMMAND_DISPATCHER_H

#include "sps_protocol.h"
#include "cmsis_os.h"

typedef enum
{
    DISPATCH_RESULT_OK = 0,
    DISPATCH_RESULT_INVALID_PARAM,
    DISPATCH_RESULT_UNKNOWN_CMD,
    DISPATCH_RESULT_QUEUE_FULL,
    DISPATCH_RESULT_PROJECTOR_ERROR,
    DISPATCH_RESULT_IR_ERROR,
    DISPATCH_RESULT_OTA_ERROR
} DispatchResult_t;

DispatchResult_t Command_DispatchFrame(const SPS_Frame_t *frame);

#endif /* COMMAND_DISPATCHER_H */
