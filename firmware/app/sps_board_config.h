#ifndef SPS_BOARD_CONFIG_H
#define SPS_BOARD_CONFIG_H

#include "stm32f4xx.h"

#define SPS_UART_PI_TX_PORT            GPIOA
#define SPS_UART_PI_TX_PIN             9u
#define SPS_UART_PI_RX_PORT            GPIOA
#define SPS_UART_PI_RX_PIN             10u
#define SPS_UART_PI_INSTANCE           USART1

#define SPS_UART_PROJECTOR_TX_PORT     GPIOA
#define SPS_UART_PROJECTOR_TX_PIN      2u
#define SPS_UART_PROJECTOR_RX_PORT     GPIOA
#define SPS_UART_PROJECTOR_RX_PIN      3u
#define SPS_UART_PROJECTOR_INSTANCE    USART2

#define SPS_RELAY_1_PORT               GPIOB
#define SPS_RELAY_1_PIN                12u
#define SPS_RELAY_2_PORT               GPIOB
#define SPS_RELAY_2_PIN                13u
#define SPS_RELAY_3_PORT               GPIOB
#define SPS_RELAY_3_PIN                14u
#define SPS_RELAY_4_PORT               GPIOB
#define SPS_RELAY_4_PIN                15u
#define SPS_RELAY_5_PORT               GPIOA
#define SPS_RELAY_5_PIN                15u

#define SPS_RELAY_ACTIVE_HIGH          1u

#define SPS_RELAY_LIGHT_MAIN           1u
#define SPS_RELAY_CURTAIN_OPEN         2u
#define SPS_RELAY_CURTAIN_CLOSE        3u
#define SPS_RELAY_SCREEN_UP            4u
#define SPS_RELAY_SCREEN_DOWN          5u

#define SPS_IR_PWM_PORT                GPIOA
#define SPS_IR_PWM_PIN                 8u
#define SPS_IR_PWM_TIM                 TIM1
#define SPS_IR_PWM_TIM_CHANNEL         1u

#endif
