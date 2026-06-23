#include "command_dispatcher.h"
#include "command_queue.h"
#include "frame_parser.h"
#include "gpio_relay_ll.h"
#include "ir_control_service.h"
#include "ota_update_manager.h"
#include "projector_service.h"
#include "relay_control_service.h"
#include "system_clock.h"
#include "telemetry_status_manager.h"
#include "timebase.h"
#include "timer_ll.h"
#include "uart_ll.h"
#include "watchdog_ll.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"

extern uint32_t __app_vector_base;

static void task_uart_rx_parser(void *argument);
static void task_watchdog(void *argument);

static StaticTask_t parser_task_tcb;
static StackType_t parser_task_stack[512];
static StaticTask_t dispatcher_task_tcb;
static StackType_t dispatcher_task_stack[768];
static StaticTask_t projector_task_tcb;
static StackType_t projector_task_stack[512];
static StaticTask_t ir_task_tcb;
static StackType_t ir_task_stack[512];
static StaticTask_t ota_task_tcb;
static StackType_t ota_task_stack[768];
static StaticTask_t telemetry_task_tcb;
static StackType_t telemetry_task_stack[512];
static StaticTask_t watchdog_task_tcb;
static StackType_t watchdog_task_stack[160];
static StaticTask_t idle_task_tcb;
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

int main(void)
{
    __disable_irq();
    system_clock_set_vector_table((uint32_t)&__app_vector_base);
    (void)system_clock_config_84mhz_hsi();
    timebase_init();
    timer_ll_init_cycle_counter();
    __enable_irq();

    uart_ll_init();
    relay_control_service_init();
    projector_service_init();
    ir_control_service_init();
    ota_update_manager_init();
    command_queue_init();
    telemetry_status_manager_init();
    watchdog_ll_init(4000u);

    (void)xTaskCreateStatic(task_uart_rx_parser,
                            "uart_parser",
                            sizeof(parser_task_stack) / sizeof(parser_task_stack[0]),
                            0,
                            tskIDLE_PRIORITY + 4u,
                            parser_task_stack,
                            &parser_task_tcb);
    (void)xTaskCreateStatic(command_dispatcher_task,
                            "dispatcher",
                            sizeof(dispatcher_task_stack) / sizeof(dispatcher_task_stack[0]),
                            0,
                            tskIDLE_PRIORITY + 3u,
                            dispatcher_task_stack,
                            &dispatcher_task_tcb);
    (void)xTaskCreateStatic(projector_service_task,
                            "projector",
                            sizeof(projector_task_stack) / sizeof(projector_task_stack[0]),
                            0,
                            tskIDLE_PRIORITY + 2u,
                            projector_task_stack,
                            &projector_task_tcb);
    (void)xTaskCreateStatic(ir_control_service_task,
                            "ir",
                            sizeof(ir_task_stack) / sizeof(ir_task_stack[0]),
                            0,
                            tskIDLE_PRIORITY + 2u,
                            ir_task_stack,
                            &ir_task_tcb);
    (void)xTaskCreateStatic(ota_update_manager_task,
                            "ota",
                            sizeof(ota_task_stack) / sizeof(ota_task_stack[0]),
                            0,
                            tskIDLE_PRIORITY + 2u,
                            ota_task_stack,
                            &ota_task_tcb);
    (void)xTaskCreateStatic(telemetry_task,
                            "telemetry",
                            sizeof(telemetry_task_stack) / sizeof(telemetry_task_stack[0]),
                            0,
                            tskIDLE_PRIORITY + 1u,
                            telemetry_task_stack,
                            &telemetry_task_tcb);
    (void)xTaskCreateStatic(task_watchdog,
                            "watchdog",
                            sizeof(watchdog_task_stack) / sizeof(watchdog_task_stack[0]),
                            0,
                            tskIDLE_PRIORITY + 1u,
                            watchdog_task_stack,
                            &watchdog_task_tcb);

    telemetry_status_manager_set_system_state(SPS_SYSTEM_READY);
    vTaskStartScheduler();

    for (;;)
    {
    }
}

static void task_uart_rx_parser(void *argument)
{
    frame_parser_t parser;
    sps_frame_t frame;

    (void)argument;
    frame_parser_init(&parser);

    for (;;)
    {
        uint8_t byte;

        if (uart_ll_read_byte(UART_LL_PORT_SBC, &byte))
        {
            frame_parser_result_t result = frame_parser_push(&parser, byte, &frame);
            if (result == FRAME_PARSER_FRAME_READY)
            {
                if (frame.msg_type == SPS_MSG_PING)
                {
                    (void)telemetry_send_pong(frame.seq);
                }
                else if (!command_queue_push(&frame))
                {
                    (void)telemetry_send_response(frame.seq, frame.device_id, frame.command_id, SPS_STATUS_BUSY, SPS_ERR_QUEUE_FULL, 0, 0u);
                }
            }
            else if (result == FRAME_PARSER_ERROR)
            {
                const frame_parser_error_t *error = frame_parser_get_error(&parser);
                if (error != 0)
                {
                    (void)telemetry_send_error(error->last_seq, error->last_device_id, error->last_command_id, error->last_error);
                }
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(1u));
        }
    }
}

static void task_watchdog(void *argument)
{
    (void)argument;

    for (;;)
    {
        watchdog_ll_feed();
        vTaskDelay(pdMS_TO_TICKS(500u));
    }
}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    gpio_relay_ll_all_off();
    for (;;)
    {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    taskDISABLE_INTERRUPTS();
    gpio_relay_ll_all_off();
    for (;;)
    {
    }
}
