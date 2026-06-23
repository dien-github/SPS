#ifndef UART_LL_H
#define UART_LL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    UART_LL_PORT_SBC = 0,
    UART_LL_PORT_PROJECTOR = 1
} uart_ll_port_t;

void uart_ll_init(void);
bool uart_ll_read_byte(uart_ll_port_t port, uint8_t *byte);
bool uart_ll_write(uart_ll_port_t port, const uint8_t *data, size_t length, uint32_t timeout_ms);
void uart_ll_irq_handler(uart_ll_port_t port);
uint32_t uart_ll_rx_overflows(uart_ll_port_t port);

#endif
