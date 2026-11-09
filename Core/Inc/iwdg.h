/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    iwdg.h
  * @brief   This file contains all the function prototypes for
  *          the iwdg.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
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
#ifndef __IWDG_H__
#define __IWDG_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern IWDG_HandleTypeDef hiwdg;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_IWDG_Init(void);

/* USER CODE BEGIN Prototypes */
/* ===== 主循环看门狗守卫（任务位图 + TIM3 tick 活性）=====
 * 每个周期任务执行完置一位；iwdg_guard_feed() 在每轮末尾仅当所有任务
 * 都完成、且 TIM3 tick 在 500ms 内推进过时才刷新 IWDG。*/
#define IWDG_TASK_KEY       (1UL << 0)
#define IWDG_TASK_RECOVERY  (1UL << 1)
#define IWDG_TASK_DISPLAY   (1UL << 2)
#define IWDG_TASK_UART1     (1UL << 3)
#define IWDG_TASK_UART2     (1UL << 4)
#define IWDG_TASK_OUTPUT    (1UL << 5)
#define IWDG_TASK_ALL       (IWDG_TASK_KEY | IWDG_TASK_RECOVERY | IWDG_TASK_DISPLAY | \
                             IWDG_TASK_UART1 | IWDG_TASK_UART2 | IWDG_TASK_OUTPUT)

void iwdg_guard_init(void);
void iwdg_guard_task(uint32_t task_bit);
void iwdg_guard_feed(uint8_t tim3_tick);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __IWDG_H__ */

