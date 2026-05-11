#ifndef IR_SERVICE_H
#define IR_SERVICE_H

#include <stdint.h>

typedef enum
{
    IR_RESULT_OK = 0,
    IR_RESULT_INVALID_PARAM
} IrResult_t;

#define IR_AC_ACTION_OFF  0x00U
#define IR_AC_ACTION_ON   0x01U

void IR_Service_Init(void);
IrResult_t IR_Service_ControlAC(uint8_t acId, uint8_t action);

#endif /* IR_SERVICE_H */
