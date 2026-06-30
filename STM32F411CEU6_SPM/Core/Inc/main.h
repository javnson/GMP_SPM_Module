/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SYS_LED_Pin GPIO_PIN_13
#define SYS_LED_GPIO_Port GPIOC
#define CTRL_DC_OUTPUT_Pin GPIO_PIN_15
#define CTRL_DC_OUTPUT_GPIO_Port GPIOC
#define SC_CHARGE_EN_Pin GPIO_PIN_2
#define SC_CHARGE_EN_GPIO_Port GPIOB
#define SC_OUTPUT_EN_Pin GPIO_PIN_14
#define SC_OUTPUT_EN_GPIO_Port GPIOB
#define BTN3_Pin GPIO_PIN_15
#define BTN3_GPIO_Port GPIOA
#define BTN2_Pin GPIO_PIN_4
#define BTN2_GPIO_Port GPIOB
#define BNT1_Pin GPIO_PIN_5
#define BNT1_GPIO_Port GPIOB
#define CTRL_DC_INPUT_Pin GPIO_PIN_9
#define CTRL_DC_INPUT_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
