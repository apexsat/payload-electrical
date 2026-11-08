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
#include "stm32g4xx_hal.h"

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

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define DISCHARGE_PGOOD_Pin GPIO_PIN_13
#define DISCHARGE_PGOOD_GPIO_Port GPIOC
#define IGNITER_PGOOD_Pin GPIO_PIN_14
#define IGNITER_PGOOD_GPIO_Port GPIOC
#define PGOOD_12V_Pin GPIO_PIN_2
#define PGOOD_12V_GPIO_Port GPIOA
#define DRDY_Pin GPIO_PIN_4
#define DRDY_GPIO_Port GPIOC
#define ARM_SAFE_Pin GPIO_PIN_5
#define ARM_SAFE_GPIO_Port GPIOC
#define WDG_IN_Pin GPIO_PIN_2
#define WDG_IN_GPIO_Port GPIOB
#define HTR2_EN_Pin GPIO_PIN_10
#define HTR2_EN_GPIO_Port GPIOB
#define DISCHARGE_EN_Pin GPIO_PIN_13
#define DISCHARGE_EN_GPIO_Port GPIOB
#define IGNITER_EN_Pin GPIO_PIN_15
#define IGNITER_EN_GPIO_Port GPIOB
#define IGNITER_PULSE_Pin GPIO_PIN_6
#define IGNITER_PULSE_GPIO_Port GPIOC
#define VBAT_EN_Pin GPIO_PIN_10
#define VBAT_EN_GPIO_Port GPIOA
#define VBAT_PGOOD_Pin GPIO_PIN_11
#define VBAT_PGOOD_GPIO_Port GPIOA
#define PD_RST_Pin GPIO_PIN_12
#define PD_RST_GPIO_Port GPIOA
#define DPT_TANK_FRIT_SEL_Pin GPIO_PIN_15
#define DPT_TANK_FRIT_SEL_GPIO_Port GPIOA
#define DPT_SCK_Pin GPIO_PIN_10
#define DPT_SCK_GPIO_Port GPIOC
#define DPT_MISO_Pin GPIO_PIN_11
#define DPT_MISO_GPIO_Port GPIOC
#define DPT_MOSI_Pin GPIO_PIN_12
#define DPT_MOSI_GPIO_Port GPIOC
#define DEBUG_LED_Pin GPIO_PIN_2
#define DEBUG_LED_GPIO_Port GPIOD
#define VALVE1_L_Pin GPIO_PIN_3
#define VALVE1_L_GPIO_Port GPIOB
#define VALVE1_H_Pin GPIO_PIN_4
#define VALVE1_H_GPIO_Port GPIOB
#define VALVE2_EN_Pin GPIO_PIN_5
#define VALVE2_EN_GPIO_Port GPIOB
#define VALVE3_L_Pin GPIO_PIN_6
#define VALVE3_L_GPIO_Port GPIOB
#define VALVE3_H_Pin GPIO_PIN_7
#define VALVE3_H_GPIO_Port GPIOB
#define HTR1_EN_Pin GPIO_PIN_9
#define HTR1_EN_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
