#include "pwm_ir_ll.h"
#include "sps_board_config.h"
#include "sps_config.h"
#include "timer_ll.h"
#include "stm32f4xx.h"

static void gpio_config_tim1_ch1(void)
{
    uint32_t pin = SPS_IR_PWM_PIN;
    uint32_t pos = pin * 2u;
    uint32_t afr_pos = (pin & 7u) * 4u;

    SPS_IR_PWM_PORT->MODER &= ~(3u << pos);
    SPS_IR_PWM_PORT->MODER |= (2u << pos);
    SPS_IR_PWM_PORT->OTYPER &= ~(1u << pin);
    SPS_IR_PWM_PORT->OSPEEDR |= (3u << pos);
    SPS_IR_PWM_PORT->PUPDR &= ~(3u << pos);
    SPS_IR_PWM_PORT->AFR[1] &= ~(0xFu << afr_pos);
    SPS_IR_PWM_PORT->AFR[1] |= (1u << afr_pos);
}

void pwm_ir_ll_init(void)
{
    uint32_t arr;
    uint32_t ccr;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    (void)RCC->APB2ENR;

    gpio_config_tim1_ch1();

    arr = (SPS_APB2_TIMER_CLOCK_HZ / SPS_IR_CARRIER_HZ) - 1u;
    ccr = ((arr + 1u) * SPS_IR_DUTY_PERMILLE) / 1000u;

    SPS_IR_PWM_TIM->PSC = 0u;
    SPS_IR_PWM_TIM->ARR = (uint16_t)arr;
    SPS_IR_PWM_TIM->CCR1 = (uint16_t)ccr;
    SPS_IR_PWM_TIM->CCMR1 &= ~TIM_CCMR1_OC1M;
    SPS_IR_PWM_TIM->CCMR1 |= (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    SPS_IR_PWM_TIM->CCER &= ~TIM_CCER_CC1E;
    SPS_IR_PWM_TIM->BDTR |= TIM_BDTR_MOE;
    SPS_IR_PWM_TIM->CR1 |= TIM_CR1_ARPE | TIM_CR1_CEN;
    SPS_IR_PWM_TIM->EGR = TIM_EGR_UG;
}

void pwm_ir_ll_carrier_on(void)
{
    SPS_IR_PWM_TIM->CCER |= TIM_CCER_CC1E;
}

void pwm_ir_ll_carrier_off(void)
{
    SPS_IR_PWM_TIM->CCER &= ~TIM_CCER_CC1E;
}

bool pwm_ir_ll_send_raw_blocking(const ir_pulse_t *pulses, size_t count)
{
    if ((pulses == 0) || (count == 0u))
    {
        return false;
    }

    for (size_t i = 0u; i < count; i++)
    {
        if (pulses[i].mark_us > 0u)
        {
            pwm_ir_ll_carrier_on();
            timer_ll_delay_us(pulses[i].mark_us);
        }
        pwm_ir_ll_carrier_off();
        if (pulses[i].space_us > 0u)
        {
            timer_ll_delay_us(pulses[i].space_us);
        }
    }

    pwm_ir_ll_carrier_off();
    return true;
}
