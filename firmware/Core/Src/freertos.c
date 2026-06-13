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
  * If no LICENSE file comes with the software, it is provided AS-IS.
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
#include "usart.h"
#include "sps_protocol.h"
#include "command_dispatcher.h"
#include "relay_service.h"
#include "ir_service.h"
#include "projector_service.h"
#include "ota_service.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define UART_RX_DMA_BUFFER_SIZE 256U
#define UART_QUEUE_SIZE         256U
#define RELAY_QUEUE_SIZE        10U
#define PROJECTOR_QUEUE_SIZE    5U
#define IR_QUEUE_SIZE           5U
#define OTA_QUEUE_SIZE          5U
#define UART_TX_TIMEOUT_MS      100U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
static uint8_t uartRxDmaBuffer[UART_RX_DMA_BUFFER_SIZE];
/* USER CODE END Variables */

/* Definitions for uartRxTask */
osThreadId_t uartRxTaskHandle;
const osThreadAttr_t uartRxTask_attributes = {
  .name = "uartRxTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};

/* Definitions for commandTask */
osThreadId_t commandTaskHandle;
const osThreadAttr_t commandTask_attributes = {
  .name = "commandTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};

/* Definitions for relayTask */
osThreadId_t relayTaskHandle;
const osThreadAttr_t relayTask_attributes = {
  .name = "relayTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for projectorTask */
osThreadId_t projectorTaskHandle;
const osThreadAttr_t projectorTask_attributes = {
  .name = "projectorTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for irTask */
osThreadId_t irTaskHandle;
const osThreadAttr_t irTask_attributes = {
  .name = "irTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for otaTask */
osThreadId_t otaTaskHandle;
const osThreadAttr_t otaTask_attributes = {
  .name = "otaTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for uartQueue */
osMessageQueueId_t uartQueueHandle;
const osMessageQueueAttr_t uartQueue_attributes = {
  .name = "uartQueue"
};

/* Definitions for relayQueue */
osMessageQueueId_t relayQueueHandle;
const osMessageQueueAttr_t relayQueue_attributes = {
  .name = "relayQueue"
};

/* Definitions for projectorQueue */
osMessageQueueId_t projectorQueueHandle;
const osMessageQueueAttr_t projectorQueue_attributes = {
  .name = "projectorQueue"
};

/* Definitions for irQueue */
osMessageQueueId_t irQueueHandle;
const osMessageQueueAttr_t irQueue_attributes = {
  .name = "irQueue"
};

/* Definitions for otaQueue */
osMessageQueueId_t otaQueueHandle;
const osMessageQueueAttr_t otaQueue_attributes = {
  .name = "otaQueue"
};

/* Definitions for uartTxMutex */
osMutexId_t uartTxMutexHandle;
const osMutexAttr_t uartTxMutex_attributes = {
  .name = "uartTxMutex"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void App_SendAck(uint8_t sourceCmd);
static void App_SendNack(uint8_t sourceCmd, uint8_t errorCode);
static void App_SendFrame(uint8_t cmdId, const uint8_t *payload, uint8_t length);
static void App_HandleRelayStatus(const SPS_Frame_t *frame);
/* USER CODE END FunctionPrototypes */

void StartUartRxTask(void *argument);
void StartCommandTask(void *argument);
void StartRelayTask(void *argument);
void StartProjectorTask(void *argument);
void StartIrTask(void *argument);
void StartOtaTask(void *argument);

void MX_FREERTOS_Init(void);

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void)
{
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  uartTxMutexHandle = osMutexNew(&uartTxMutex_attributes);
  configASSERT(uartTxMutexHandle != NULL);
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */

  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */

  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  uartQueueHandle = osMessageQueueNew(UART_QUEUE_SIZE, sizeof(uint8_t), &uartQueue_attributes);
  relayQueueHandle = osMessageQueueNew(RELAY_QUEUE_SIZE, sizeof(uint32_t), &relayQueue_attributes);
  projectorQueueHandle = osMessageQueueNew(PROJECTOR_QUEUE_SIZE, sizeof(ProjectorCommand_t), &projectorQueue_attributes);
  irQueueHandle = osMessageQueueNew(IR_QUEUE_SIZE, sizeof(IrCommand_t), &irQueue_attributes);
  otaQueueHandle = osMessageQueueNew(OTA_QUEUE_SIZE, sizeof(OtaCommand_t), &otaQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  configASSERT(uartQueueHandle != NULL);
  configASSERT(relayQueueHandle != NULL);
  configASSERT(projectorQueueHandle != NULL);
  configASSERT(irQueueHandle != NULL);
  configASSERT(otaQueueHandle != NULL);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  uartRxTaskHandle = osThreadNew(StartUartRxTask, NULL, &uartRxTask_attributes);
  commandTaskHandle = osThreadNew(StartCommandTask, NULL, &commandTask_attributes);
  relayTaskHandle = osThreadNew(StartRelayTask, NULL, &relayTask_attributes);
  projectorTaskHandle = osThreadNew(StartProjectorTask, NULL, &projectorTask_attributes);
  irTaskHandle = osThreadNew(StartIrTask, NULL, &irTask_attributes);
  otaTaskHandle = osThreadNew(StartOtaTask, NULL, &otaTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  configASSERT(uartRxTaskHandle != NULL);
  configASSERT(commandTaskHandle != NULL);
  configASSERT(relayTaskHandle != NULL);
  configASSERT(projectorTaskHandle != NULL);
  configASSERT(irTaskHandle != NULL);
  configASSERT(otaTaskHandle != NULL);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */

  /* USER CODE END RTOS_EVENTS */
}

/* USER CODE BEGIN Header_StartUartRxTask */
/**
  * @brief  Function implementing the uartRxTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartUartRxTask */
void StartUartRxTask(void *argument)
{
  /* USER CODE BEGIN StartUartRxTask */

  HAL_UARTEx_ReceiveToIdle_DMA(&huart1,
                               uartRxDmaBuffer,
                               UART_RX_DMA_BUFFER_SIZE);
  __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);

  for (;;)
  {
    osDelay(1000U);
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
void StartCommandTask(void *argument)
{
  /* USER CODE BEGIN StartCommandTask */

  uint8_t rxByte;
  SPS_Frame_t frame;

  SPS_Protocol_Init();

  for (;;)
  {
    if (osMessageQueueGet(uartQueueHandle, &rxByte, NULL, osWaitForever) == osOK)
    {
      SPS_ParseResult_t parseResult;

      parseResult = SPS_Protocol_ParseByte(rxByte, &frame);

      if (parseResult == SPS_PARSE_FRAME_READY)
      {
        if (frame.cmd_id == SPS_CMD_PING)
        {
          App_SendAck(SPS_CMD_PING);
        }
        else if (frame.cmd_id == SPS_CMD_RELAY_STATUS)
        {
          App_HandleRelayStatus(&frame);
        }
        else
        {
          DispatchResult_t dispatchResult;

          dispatchResult = Command_DispatchFrame(&frame);

          if (dispatchResult == DISPATCH_RESULT_INVALID_PARAM)
          {
            App_SendNack(frame.cmd_id, SPS_ERR_INVALID_PARAM);
          }
          else if (dispatchResult == DISPATCH_RESULT_QUEUE_FULL)
          {
            App_SendNack(frame.cmd_id, SPS_ERR_QUEUE_FULL);
          }
          else if (dispatchResult == DISPATCH_RESULT_UNKNOWN_CMD)
          {
            App_SendNack(frame.cmd_id, SPS_ERR_UNKNOWN_CMD);
          }
          else if (dispatchResult == DISPATCH_RESULT_PROJECTOR_ERROR)
          {
            App_SendNack(frame.cmd_id, SPS_ERR_TIMEOUT);
          }
          else if (dispatchResult == DISPATCH_RESULT_IR_ERROR)
          {
            App_SendNack(frame.cmd_id, SPS_ERR_TIMEOUT);
          }
          else if (dispatchResult == DISPATCH_RESULT_OTA_ERROR)
          {
            App_SendNack(frame.cmd_id, SPS_ERR_OTA);
          }
          else
          {
        	  /*
        	   * Các command có task riêng sẽ không ACK ở commandTask:
        	   * - Relay: relayTask ACK sau khi GPIO thực thi xong.
        	   * - Projector: projectorTask ACK sau khi USART2 xử lý xong.
        	   * - IR: irTask ACK sau khi phát IR xong.
        	   * - OTA: otaTask ACK sau khi xử lý OTA xong.
        	   */
            /*
             * Relay command không ACK ở commandTask.
             * relayTask sẽ ACK sau khi GPIO thực thi xong.
             */
          }
        }
      }
      else if (parseResult == SPS_PARSE_ERROR)
      {
        App_SendNack(0x00U, SPS_ERR_CRC);
      }
      else
      {
        /* Frame chưa đủ byte. */
      }
    }
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
void StartRelayTask(void *argument)
{
  /* USER CODE BEGIN StartRelayTask */

  uint32_t relayCmd;
  RelayResult_t result;
  uint8_t sourceCmd;

  Relay_Service_Init();

  for (;;)
  {
    if (osMessageQueueGet(relayQueueHandle, &relayCmd, NULL, 100U) == osOK)
    {
      sourceCmd = RELAY_CMD_GET_SOURCE(relayCmd);
      result = Relay_Service_ExecuteCommand(relayCmd);

      if (result == RELAY_RESULT_OK)
      {
        App_SendAck(sourceCmd);
      }
      else
      {
        App_SendNack(sourceCmd, SPS_ERR_INVALID_PARAM);
      }
    }

    Relay_Service_Periodic();
  }

  /* USER CODE END StartRelayTask */
}
/* USER CODE BEGIN Header_StartProjectorTask */
/**
* @brief Function implementing the projectorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartProjectorTask */
void StartProjectorTask(void *argument)
{
  /* USER CODE BEGIN StartProjectorTask */

  ProjectorCommand_t projectorCmd;
  ProjectorResult_t result;

  (void)argument;

  Projector_Service_Init();

  for (;;)
  {
    if (osMessageQueueGet(projectorQueueHandle, &projectorCmd, NULL, osWaitForever) == osOK)
    {
      result = Projector_Service_ExecuteCommand(&projectorCmd);

      if (result == PROJECTOR_RESULT_OK)
      {
        App_SendAck(projectorCmd.sourceCmd);
      }
      else if (result == PROJECTOR_RESULT_INVALID_PARAM)
      {
        App_SendNack(projectorCmd.sourceCmd, SPS_ERR_INVALID_PARAM);
      }
      else
      {
        App_SendNack(projectorCmd.sourceCmd, SPS_ERR_TIMEOUT);
      }
    }
  }

  /* USER CODE END StartProjectorTask */
}

/* USER CODE BEGIN Header_StartIrTask */
/**
* @brief Function implementing the irTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartIrTask */
void StartIrTask(void *argument)
{
  /* USER CODE BEGIN StartIrTask */

  IrCommand_t irCmd;
  IrResult_t result;

  (void)argument;

  IR_Service_Init();

  for (;;)
  {
    if (osMessageQueueGet(irQueueHandle, &irCmd, NULL, osWaitForever) == osOK)
    {
      result = IR_Service_ExecuteCommand(&irCmd);

      if (result == IR_RESULT_OK)
      {
        App_SendAck(irCmd.sourceCmd);
      }
      else
      {
        App_SendNack(irCmd.sourceCmd, SPS_ERR_INVALID_PARAM);
      }
    }
  }

  /* USER CODE END StartIrTask */
}

/* USER CODE BEGIN Header_StartOtaTask */
/**
* @brief Function implementing the otaTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartOtaTask */
void StartOtaTask(void *argument)
{
  /* USER CODE BEGIN StartOtaTask */

  OtaCommand_t otaCmd;
  OtaResult_t result;

  (void)argument;

  OTA_Service_Init();

  for (;;)
  {
    if (osMessageQueueGet(otaQueueHandle, &otaCmd, NULL, osWaitForever) == osOK)
    {
      result = OTA_Service_ExecuteCommand(&otaCmd);

      if (result == OTA_RESULT_OK)
      {
        App_SendAck(otaCmd.sourceCmd);
      }
      else if (result == OTA_RESULT_INVALID_PARAM)
      {
        App_SendNack(otaCmd.sourceCmd, SPS_ERR_INVALID_PARAM);
      }
      else
      {
        App_SendNack(otaCmd.sourceCmd, SPS_ERR_OTA);
      }
    }
  }

  /* USER CODE END StartOtaTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  if (huart->Instance == USART1)
  {
    for (uint16_t i = 0U; i < Size; i++)
    {
      uint8_t byte = uartRxDmaBuffer[i];

      if (osMessageQueuePut(uartQueueHandle, &byte, 0U, 0U) != osOK)
      {
        /*
         * Queue đầy: bỏ phần còn lại của block hiện tại.
         * Nếu gặp thực tế, tăng UART_QUEUE_SIZE hoặc giảm tốc độ gửi từ SBC.
         */
        break;
      }
    }

    HAL_UARTEx_ReceiveToIdle_DMA(&huart1,
                                 uartRxDmaBuffer,
                                 UART_RX_DMA_BUFFER_SIZE);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
  }
}

static void App_SendFrame(uint8_t cmdId, const uint8_t *payload, uint8_t length)
{
  uint8_t txBuffer[SPS_MAX_FRAME_SIZE];
  uint16_t frameSize;

  frameSize = SPS_Protocol_BuildFrame(cmdId,
                                      payload,
                                      length,
                                      txBuffer,
                                      sizeof(txBuffer));

  if (frameSize == 0U)
  {
    return;
  }

  if (osMutexAcquire(uartTxMutexHandle, UART_TX_TIMEOUT_MS) == osOK)
  {
    (void)HAL_UART_Transmit(&huart1,
                            txBuffer,
                            frameSize,
                            UART_TX_TIMEOUT_MS);

    (void)osMutexRelease(uartTxMutexHandle);
  }
}

static void App_SendAck(uint8_t sourceCmd)
{
  uint8_t payload[1];

  payload[0] = sourceCmd;

  App_SendFrame(SPS_CMD_ACK, payload, 1U);
}

static void App_SendNack(uint8_t sourceCmd, uint8_t errorCode)
{
  uint8_t payload[2];

  payload[0] = sourceCmd;
  payload[1] = errorCode;

  App_SendFrame(SPS_CMD_NACK, payload, 2U);
}

static void App_HandleRelayStatus(const SPS_Frame_t *frame)
{
  uint8_t payload[2];

  if (frame == NULL)
  {
    App_SendNack(0x00U, SPS_ERR_INVALID_PARAM);
    return;
  }

  if (frame->length != 1U)
  {
    App_SendNack(frame->cmd_id, SPS_ERR_INVALID_PARAM);
    return;
  }

  payload[0] = frame->payload[0];

  if (frame->payload[0] == RELAY_DEVICE_LIGHT_1 ||
      frame->payload[0] == RELAY_DEVICE_LIGHT_ALL)
  {
    payload[1] = Relay_Service_GetLightState();
  }
  else if (frame->payload[0] == RELAY_DEVICE_CURTAIN)
  {
    payload[1] = Relay_Service_GetCurtainState();
  }
  else if (frame->payload[0] == RELAY_DEVICE_SCREEN)
  {
    payload[1] = Relay_Service_GetScreenState();
  }
  else
  {
    App_SendNack(frame->cmd_id, SPS_ERR_INVALID_PARAM);
    return;
  }

  App_SendFrame(SPS_CMD_RELAY_STATUS, payload, 2U);
}

/* USER CODE END Application */
