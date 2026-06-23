#include "uart_ll.h"
#include "ring_buffer.h"
#include "sps_board_config.h"
#include "sps_config.h"
#include "timebase.h"
#include "stm32f4xx.h"

static uint8_t sbc_rx_storage[SPS_UART_RX_RING_SIZE];
static uint8_t projector_rx_storage[256u];
static ring_buffer_t sbc_rx_ring;
static ring_buffer_t projector_rx_ring;

static void gpio_config_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    uint32_t pos = pin * 2u;
    uint32_t afr_pos = (pin & 7u) * 4u;
    volatile uint32_t *afr = (pin < 8u) ? &port->AFR[0] : &port->AFR[1];

    port->MODER &= ~(3u << pos);
    port->MODER |= (2u << pos);
    port->OTYPER &= ~(1u << pin);
    port->OSPEEDR |= (3u << pos);
    port->PUPDR &= ~(3u << pos);
    port->PUPDR |= (1u << pos);
    *afr &= ~(0xFu << afr_pos);
    *afr |= (af << afr_pos);
}

static void usart_config(USART_TypeDef *uart, uint32_t pclk_hz, uint32_t baudrate)
{
    uart->CR1 = 0u;
    uart->CR2 = 0u;
    uart->CR3 = 0u;
    uart->BRR = (pclk_hz + (baudrate / 2u)) / baudrate;
    uart->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
}

void uart_ll_init(void)
{
    ring_buffer_init(&sbc_rx_ring, sbc_rx_storage, sizeof(sbc_rx_storage));
    ring_buffer_init(&projector_rx_ring, projector_rx_storage, sizeof(projector_rx_storage));

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->AHB1ENR;

    gpio_config_af(SPS_UART_PI_TX_PORT, SPS_UART_PI_TX_PIN, 7u);
    gpio_config_af(SPS_UART_PI_RX_PORT, SPS_UART_PI_RX_PIN, 7u);
    gpio_config_af(SPS_UART_PROJECTOR_TX_PORT, SPS_UART_PROJECTOR_TX_PIN, 7u);
    gpio_config_af(SPS_UART_PROJECTOR_RX_PORT, SPS_UART_PROJECTOR_RX_PIN, 7u);

    usart_config(SPS_UART_PI_INSTANCE, SPS_APB2_CLOCK_HZ, SPS_UART_SBC_BAUDRATE);
    usart_config(SPS_UART_PROJECTOR_INSTANCE, SPS_APB1_CLOCK_HZ, SPS_UART_PROJECTOR_BAUDRATE);

    NVIC_SetPriority(USART1_IRQn, 6u);
    NVIC_EnableIRQ(USART1_IRQn);
    NVIC_SetPriority(USART2_IRQn, 6u);
    NVIC_EnableIRQ(USART2_IRQn);
}

bool uart_ll_read_byte(uart_ll_port_t port, uint8_t *byte)
{
    return ring_buffer_pop((port == UART_LL_PORT_SBC) ? &sbc_rx_ring : &projector_rx_ring, byte);
}

bool uart_ll_write(uart_ll_port_t port, const uint8_t *data, size_t length, uint32_t timeout_ms)
{
    USART_TypeDef *uart = (port == UART_LL_PORT_SBC) ? SPS_UART_PI_INSTANCE : SPS_UART_PROJECTOR_INSTANCE;
    uint32_t start;

    if ((data == 0) && (length > 0u))
    {
        return false;
    }

    start = timebase_millis();
    for (size_t i = 0u; i < length; i++)
    {
        while ((uart->SR & USART_SR_TXE) == 0u)
        {
            if ((uint32_t)(timebase_millis() - start) > timeout_ms)
            {
                return false;
            }
        }
        uart->DR = data[i];
    }

    while ((uart->SR & USART_SR_TC) == 0u)
    {
        if ((uint32_t)(timebase_millis() - start) > timeout_ms)
        {
            return false;
        }
    }

    return true;
}

void uart_ll_irq_handler(uart_ll_port_t port)
{
    USART_TypeDef *uart = (port == UART_LL_PORT_SBC) ? SPS_UART_PI_INSTANCE : SPS_UART_PROJECTOR_INSTANCE;
    ring_buffer_t *rb = (port == UART_LL_PORT_SBC) ? &sbc_rx_ring : &projector_rx_ring;

    if ((uart->SR & USART_SR_RXNE) != 0u)
    {
        uint8_t byte = (uint8_t)uart->DR;
        (void)ring_buffer_push_isr(rb, byte);
    }

    if ((uart->SR & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) != 0u)
    {
        volatile uint32_t sr = uart->SR;
        volatile uint32_t dr = uart->DR;
        (void)sr;
        (void)dr;
    }
}

uint32_t uart_ll_rx_overflows(uart_ll_port_t port)
{
    const ring_buffer_t *rb = (port == UART_LL_PORT_SBC) ? &sbc_rx_ring : &projector_rx_ring;
    return rb->overflow_count;
}
