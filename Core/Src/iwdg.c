/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    iwdg.c
  * @brief   This file provides code for the configuration
  *          of the IWDG instances.
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
/* Includes ------------------------------------------------------------------*/
#include "iwdg.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

IWDG_HandleTypeDef hiwdg;

/* IWDG init function */
void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_4;
  hiwdg.Init.Reload = 999;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */
  /* BL 兼容改造（前置清单 5）：生成代码配置为 100ms（Prescaler 4 / Reload 999），
   * 运行期需覆盖 Flash 整页提交（页擦 20~40ms + 编程）等长路径 —— 放宽至 ~1s：
   * 40kHz/64 = 625Hz，Reload 624 → 625 计数 = 1.0s。main() 开头已先行喂狗并
   * 预置同值，此处覆盖 MX_IWDG_Init 写入的 100ms 配置。*/
  IWDG->KR = 0x5555u;             /* 允许写 PR/RLR */
  IWDG->PR = IWDG_PRESCALER_64;
  IWDG->RLR = 624u;
  IWDG->KR = 0xAAAAu;             /* 以新值立即重载 */
  /* USER CODE END IWDG_Init 2 */

}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
