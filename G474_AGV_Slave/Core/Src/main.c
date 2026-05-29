/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "adc.h"
#include "fdcan.h"
#include "usart.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "agv_g474.h"
#include "motor_control.h"
#include "g474_can.h"
#include "g474_debug.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
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

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void Simulate_UpdateStatus(G474_StatusFrame_t *status);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_FDCAN1_Init();
  MX_LPUART1_UART_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */
  //AGV_Init();
  //Motor_Init();
  //printf("AGV G474 Slave Boot OK\r\n");

  G474_Debug_Init();
  G474_CAN_Init();
  printf("\r\n========== G474 Slave Simulator Started ==========\r\n");
   printf("CAN bus initialized (1Mbps), filter accepts all standard IDs\r\n");


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  G474_StatusFrame_t status;
    memset(&status, 0, sizeof(status));
    status.tail = 0xAA;

    uint32_t lastSend = 0;
    uint32_t lastHeartbeatCheck = 0;
  while (1)
  {
	  /* Send status frame every 100ms */
	          if (HAL_GetTick() - lastSend >= 100)
	          {
	              lastSend = HAL_GetTick();
	              Simulate_UpdateStatus(&status);
	              if (G474_CAN_SendStatus(&status) == 0)
	              {
	                  printf("[G474] Send status ID=0x301, encoder=%d, left/right sensors=%d/%d\r\n",
	                         status.encoder_cnt, status.sensor_left, status.sensor_right);
	              }
	              else
	              {
	                  printf("[G474] Status send failed!\r\n");
	              }
	          }

	          /* Check heartbeat timeout */
	          if (HAL_GetTick() - lastHeartbeatCheck >= 500)
	          {
	              lastHeartbeatCheck = HAL_GetTick();
	              if (!G474_CAN_IsHeartbeatReceived())
	              {
	                  printf("[G474 Safety] No heartbeat from H753 for >500ms! Simulating stop all motors\r\n");
	              }
	              else
	              {
	                  G474_CAN_ClearHeartbeatFlag();
	              }
	          }

	          /* Process received CAN commands */
	          uint32_t rxId = G474_CAN_GetLastRxID();
	          if (rxId != 0)
	          {
	              uint8_t *data = G474_CAN_GetLastRxData();
	              switch (rxId)
	              {
	                  case CAN_ID_AGV_CMD:
	                      printf("[Sim exec] AGV cmd: speed=%d, dir=%s\r\n",
	                             data[0], data[1] ? "reverse" : "forward");
	                      break;
	                  case CAN_ID_CONVEYOR_CMD:
	                      printf("[Sim exec] Conveyor cmd: run=%d, speed=%d, dir=%s\r\n",
	                             data[0], data[1], data[2] ? "reverse" : "forward");
	                      break;
	                  case CAN_ID_SERVO_CMD:
	                      printf("[Sim exec] Servo cmd: pitch=%d deg, yaw=%d deg\r\n", data[0], data[1]);
	                      break;
	                  default:
	                      break;
	              }
	              G474_CAN_ClearLastRxID();
	          }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV6;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* 模拟传感器和编码器数据（可自增或随机） */
static void Simulate_UpdateStatus(G474_StatusFrame_t *status)
{
    static int16_t encoder = 0;
    static uint8_t left = 50, right = 50;
    static int8_t  dc = 0;
    static uint8_t step = 0;

    encoder += 10;
    if (encoder > 3000) encoder = 0;

    /* Simple triangular wave for sensors */
    step++;
    if (step < 128) {
        left = 50 + step;
        right = 50 + 128 - step;
    } else {
        left = 50 + (255 - step);
        right = 50 + (step - 128);
    }

    dc = (dc == 0) ? 60 : 0;

    status->dc_speed     = dc;
    status->step_speed   = 100;
    status->encoder_cnt  = encoder;
    status->sensor_left  = left;
    status->sensor_right = right;
    status->sys_status   = 0;
    status->tail         = 0xAA;
}


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
