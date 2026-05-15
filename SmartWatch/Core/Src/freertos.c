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
#include "queue.h"
#include "at_driver.h"       // 引入 URC_Task 和 uart_msg_t

// 引入任务模块的头文件
#include "task_sensor.h"     
#include "task_decision.h"   
#include "task_hmi.h"  
#include "task_comm.h"
#include "task_max30102.h" 
#include "task_alert.h"
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
// --- 任务句柄 ---
osThreadId_t sensorTaskHandle;
osThreadId_t decisionTaskHandle;
osThreadId_t hmiTaskHandle;
osThreadId_t commTaskHandle;
osThreadId_t urcTaskHandle;
osThreadId_t max30102TaskHandle;
osThreadId_t alertTaskHandle;
QueueHandle_t xUartRxQueueHandle;  // 串口接收队列句柄
osMessageQueueId_t downlinkQueueHandle; // 下行命令队列句柄

// --- 任务属性 ---
const osThreadAttr_t sensorTask_attributes = {
  .name = "SensorTask",
  .stack_size = 1536,
  .priority = (osPriority_t) osPriorityAboveNormal,
};

const osThreadAttr_t decisionTask_attributes = {
  .name = "DecisionTask",
  .stack_size = 1024,
  .priority = (osPriority_t) osPriorityBelowNormal,
};

const osThreadAttr_t hmiTask_attributes = {
  .name = "HMITask",
  .stack_size = 1664,
  .priority = (osPriority_t) osPriorityNormal,
};

const osThreadAttr_t commTask_attributes = {
  .name = "CommTask",
  .stack_size = 8192,
  .priority = (osPriority_t) osPriorityBelowNormal,
};

const osThreadAttr_t urcTask_attributes = {
  .name = "URCTask",
  .stack_size = 2048,
  .priority = (osPriority_t) osPriorityBelowNormal,
};

const osThreadAttr_t max30102Task_attributes = {
  .name = "Max30102Task",
  .stack_size = 1024,
  .priority = (osPriority_t) osPriorityAboveNormal,
};

const osThreadAttr_t alertTask_attributes = {
    .name = "AlertTask",
    .stack_size = 5120,
    .priority = (osPriority_t) osPriorityHigh,
};

// --- 同步机制 ---
osMutexId_t xDataMutexHandle;  // 互斥锁句柄定义
const osMutexAttr_t xDataMutex_attributes = { .name = "xDataMutex" };

// 通信暂停信号量
osSemaphoreId_t xCommPauseSemHandle;
const osSemaphoreAttr_t xCommPauseSem_attributes = { .name = "xCommPauseSem" };


// --- 全局数据实例 ---
SensorData_t g_SensorData = {0};
SystemStatus_t g_SystemStatus = {0};
HMIInput_t g_HMIInput = {0};
FamilyConfig_t g_FamilyConfig = {0};


/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

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
  xDataMutexHandle = osMutexNew(&xDataMutex_attributes);
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  xCommPauseSemHandle = osSemaphoreNew(1, 1, &xCommPauseSem_attributes);
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  xUartRxQueueHandle = xQueueCreate(10, sizeof(uart_msg_t));
  downlinkQueueHandle = osMessageQueueNew(5, sizeof(downlink_msg_t), NULL);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
    // 直接调用各模块封装好的入口函数
  sensorTaskHandle   = osThreadNew(SensorTask, NULL, &sensorTask_attributes);
  decisionTaskHandle = osThreadNew(DecisionTask, NULL, &decisionTask_attributes);
  hmiTaskHandle      = osThreadNew(HMITask, NULL, &hmiTask_attributes);
  commTaskHandle     = osThreadNew(CommTask, NULL, &commTask_attributes);
  urcTaskHandle      = osThreadNew(URC_Task, NULL, &urcTask_attributes);
  max30102TaskHandle = osThreadNew(MAX30102_Task, NULL, &max30102Task_attributes);
  alertTaskHandle = osThreadNew(AlertTask, NULL, &alertTask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
// void StartDefaultTask(void *argument)
// {
  /* USER CODE BEGIN StartDefaultTask */
//   /* Infinite loop */
//   for(;;)
//   {
//     osDelay(1);
//   }
  /* USER CODE END StartDefaultTask */
// }

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

