#ifndef PROJECTOR_SERVICE_H
#define PROJECTOR_SERVICE_H

#include <stdint.h>

typedef enum
{
    PROJECTOR_RESULT_OK = 0,
    PROJECTOR_RESULT_INVALID_PARAM,
    PROJECTOR_RESULT_UART_ERROR
} ProjectorResult_t;

#define PROJECTOR_ACTION_OFF  0x00U
#define PROJECTOR_ACTION_ON   0x01U

typedef struct
{
    uint8_t sourceCmd;
    uint8_t action;
} ProjectorCommand_t;

void Projector_Service_Init(void);
ProjectorResult_t Projector_Service_Control(uint8_t action);
ProjectorResult_t Projector_Service_ExecuteCommand(const ProjectorCommand_t *cmd);

#endif /* PROJECTOR_SERVICE_H */
