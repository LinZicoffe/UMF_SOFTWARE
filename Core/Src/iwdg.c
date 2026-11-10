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
/* ===== 主循环看门狗守卫 =====
 * iwdg_guard_task() 记录本轮已完成的任务，iwdg_guard_feed() 在每轮末尾统一
 * 判定：任务位全齐 + TIM3 tick 在 500ms 内推进过，才刷新 IWDG。驱动内部
 * 不再喂狗，任何环节挂死（含 TIM3 中断停摆）都会在 1s 内复位。
 * 时间基准用 DWT 周期计数器而非 HAL_GetTick()：后者依赖 SysTick 中断，
 * 中断全停时会冻结，活性检查会永远通过。*/
#define IWDG_TICK_STALE_CYC (72000UL * 500UL)   /* 500ms @72MHz */

static uint32_t s_task_mask;   /* 本轮已完成任务位图 */
static uint8_t  s_tick_seen;   /* 上次看到的 TIM3 tick */
static uint32_t s_tick_stamp;  /* tick 最近一次推进的 DWT 周期戳 */

void iwdg_guard_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   /* 使能 DWT */
    DWT->CYCCNT = 0u;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    s_task_mask  = 0u;
    s_tick_seen  = 0u;
    s_tick_stamp = DWT->CYCCNT;
}

void iwdg_guard_task(uint32_t task_bit)
{
    s_task_mask |= task_bit;
}

void iwdg_guard_feed(uint8_t tim3_tick)
{
    uint32_t now = DWT->CYCCNT;

    if (tim3_tick != s_tick_seen)   /* TIM3 中断仍在推进 */
    {
        s_tick_seen  = tim3_tick;
        s_tick_stamp = now;
    }

    if ((s_task_mask == IWDG_TASK_ALL) &&
        ((uint32_t)(now - s_tick_stamp) <= IWDG_TICK_STALE_CYC))
    {
        HAL_IWDG_Refresh(&hiwdg);
    }

    s_task_mask = 0u;               /* 开启下一轮 */
}
/* USER CODE END 1 */
