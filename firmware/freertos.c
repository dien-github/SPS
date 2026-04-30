/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * File Name          : freertos.c
 * Description        : Code for freertos applications
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "relay_service.h"
#include "sps_protocol.h"
#include "command_dispatcher.h"
#include "stm32f4xx_ll_usart.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for uartRxTask */
osThreadId_t uartRxTaskHandle;
const osThreadAttr_t uartRxTask_attributes = { .name = "uartRxTask",
		.stack_size = 256 * 4, .priority = (osPriority_t) osPriorityHigh, };
/* Definitions for commandTask */
osThreadId_t commandTaskHandle;
const osThreadAttr_t commandTask_attributes = { .name = "commandTask",
		.stack_size = 256 * 4, .priority = (osPriority_t) osPriorityNormal, };
/* Definitions for relayTask */
osThreadId_t relayTaskHandle;
const osThreadAttr_t relayTask_attributes = { .name = "relayTask", .stack_size =
		256 * 4, .priority = (osPriority_t) osPriorityNormal, };
/* Definitions for statusTask */
osThreadId_t statusTaskHandle;
const osThreadAttr_t statusTask_attributes = { .name = "statusTask",
		.stack_size = 128 * 4, .priority = (osPriority_t) osPriorityLow, };
/* Definitions for relayQueue */
osMessageQueueId_t relayQueueHandle;
const osMessageQueueAttr_t relayQueue_attributes = { .name = "relayQueue" };

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartUartRxTask(void *argument);
void StartCommandTask(void *argument);
void StartRelayTask(void *argument);
void StartStatusTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
 * @brief  FreeRTOS initialization
 * @param  None
 * @retval None
 */
void MX_FREERTOS_Init(void) {
	/* USER CODE BEGIN Init */

	/* USER CODE END Init */

	/* USER CODE BEGIN RTOS_MUTEX */
	/* add mutexes, ... */
	/* USER CODE END RTOS_MUTEX */

	/* USER CODE BEGIN RTOS_SEMAPHORES */
	/* add semaphores, ... */
	/* USER CODE END RTOS_SEMAPHORES */

	/* USER CODE BEGIN RTOS_TIMERS */
	/* start timers, add new ones, ... */
	/* USER CODE END RTOS_TIMERS */

	/* Create the queue(s) */
	/* creation of relayQueue */
	relayQueueHandle = osMessageQueueNew(8, sizeof(uint32_t),
			&relayQueue_attributes);

	/* USER CODE BEGIN RTOS_QUEUES */
	/* add queues, ... */
	/* USER CODE END RTOS_QUEUES */

	/* Create the thread(s) */
	/* creation of uartRxTask */
	uartRxTaskHandle = osThreadNew(StartUartRxTask, NULL,
			&uartRxTask_attributes);

	/* creation of commandTask */
	commandTaskHandle = osThreadNew(StartCommandTask, NULL,
			&commandTask_attributes);

	/* creation of relayTask */
	relayTaskHandle = osThreadNew(StartRelayTask, NULL, &relayTask_attributes);

	/* creation of statusTask */
	statusTaskHandle = osThreadNew(StartStatusTask, NULL,
			&statusTask_attributes);

	/* USER CODE BEGIN RTOS_THREADS */
	/* add threads, ... */
	/* USER CODE END RTOS_THREADS */

	/* USER CODE BEGIN RTOS_EVENTS */
	/* add events, ... */
	/* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartUartRxTask */
/**
 * @brief  Function implementing the uartRxTask thread.
 * @param  argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartUartRxTask */
void StartUartRxTask(void *argument) {
	/* USER CODE BEGIN StartUartRxTask */
	sps_parser_t parser;
	  sps_frame_t frame;
	  uint8_t rx_byte;
	  int32_t parse_result;

	  SPS_Protocol_ParserInit(&parser);

	  for (;;)
	  {
	    while (LL_USART_IsActiveFlag_RXNE(USART1) != 0U)
	    {
	      rx_byte = LL_USART_ReceiveData8(USART1);

	      parse_result = SPS_Protocol_ParserPushByte(&parser, rx_byte, &frame);

	      if (parse_result == 1)
	      {
	        Command_DispatchFrame(&frame);
	      }
	      else if (parse_result < 0)
	      {
	        SPS_Protocol_SendNack(0x00U, SPS_ERR_CRC);
	      }
	      else
	      {
	        /* Frame chua hoan thanh, tiep tuc nhan byte */
	      }
	    }

	    osDelay(1U);
	  }
	/* USER CODE END StartUartRxTask */
}

/* USER CODE BEGIN Header_StartCommandTask */
/**
 * @brief Function implementing the commandTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartCommandTask */
void StartCommandTask(void *argument) {
	/* USER CODE BEGIN StartCommandTask */
	for (;;)
	  {
	    /*
	     * Version hien tai:
	     * - UartRxTask nhan byte, parse frame va dispatch command.
	     * - CommandTask du phong de sau nay tach parser/dispatcher rieng.
	     */
	    osDelay(1000U);
	  }
	/* USER CODE END StartCommandTask */
}

/* USER CODE BEGIN Header_StartRelayTask */
/**
 * @brief Function implementing the relayTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartRelayTask */
void StartRelayTask(void *argument) {
	/* USER CODE BEGIN StartRelayTask */
	uint32_t relay_cmd;
	  relay_result_t result;

	  Relay_Service_Init();

	  for (;;)
	  {
	    if (osMessageQueueGet(relayQueueHandle, &relay_cmd, NULL, 100U) == osOK)
	    {
	      result = Relay_Service_ExecuteCommand(relay_cmd);
	      (void)result;
	    }

	    Relay_Service_Periodic();
	  }
	/* USER CODE END StartRelayTask */
}

/* USER CODE BEGIN Header_StartStatusTask */
/**
 * @brief Function implementing the statusTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartStatusTask */
void StartStatusTask(void *argument) {
	/* USER CODE BEGIN StartStatusTask */
	/* Infinite loop */
	for (;;) {
		osDelay(1);
	}
	/* USER CODE END StartStatusTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

