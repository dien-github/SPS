#include "gpio_relay_ll.h"
#include "sps_board_config.h"
#include "stm32f4xx.h"

typedef struct
{
    GPIO_TypeDef *port;
    uint8_t pin;
    bool state;
} relay_hw_t;

static relay_hw_t relays[] =
{
    { SPS_RELAY_1_PORT, SPS_RELAY_1_PIN, false },
    { SPS_RELAY_2_PORT, SPS_RELAY_2_PIN, false },
    { SPS_RELAY_3_PORT, SPS_RELAY_3_PIN, false },
    { SPS_RELAY_4_PORT, SPS_RELAY_4_PIN, false },
    { SPS_RELAY_5_PORT, SPS_RELAY_5_PIN, false }
};

static void gpio_config_output(GPIO_TypeDef *port, uint8_t pin)
{
    uint32_t pos = (uint32_t)pin * 2u;

    port->MODER &= ~(3u << pos);
    port->MODER |= (1u << pos);
    port->OTYPER &= ~(1u << pin);
    port->OSPEEDR |= (2u << pos);
    port->PUPDR &= ~(3u << pos);
}

static void relay_write(relay_hw_t *relay, bool on)
{
    bool pin_high = (SPS_RELAY_ACTIVE_HIGH != 0u) ? on : !on;
    uint32_t pin_mask = 1u << relay->pin;

    if (pin_high)
    {
        relay->port->BSRR = pin_mask;
    }
    else
    {
        relay->port->BSRR = pin_mask << 16u;
    }
    relay->state = on;
}

void gpio_relay_ll_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;

    for (uint8_t i = 0u; i < (uint8_t)(sizeof(relays) / sizeof(relays[0])); i++)
    {
        gpio_config_output(relays[i].port, relays[i].pin);
        relay_write(&relays[i], false);
    }
}

void gpio_relay_ll_all_off(void)
{
    for (uint8_t i = 0u; i < (uint8_t)(sizeof(relays) / sizeof(relays[0])); i++)
    {
        relay_write(&relays[i], false);
    }
}

bool gpio_relay_ll_set(uint8_t channel, bool on)
{
    if ((channel == 0u) || (channel > (uint8_t)(sizeof(relays) / sizeof(relays[0]))))
    {
        return false;
    }

    relay_write(&relays[channel - 1u], on);
    return true;
}

bool gpio_relay_ll_get(uint8_t channel)
{
    if ((channel == 0u) || (channel > (uint8_t)(sizeof(relays) / sizeof(relays[0]))))
    {
        return false;
    }

    return relays[channel - 1u].state;
}
