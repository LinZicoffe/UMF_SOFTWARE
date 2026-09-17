/**
 * @file    bl_clock.h
 * @brief   BL 时钟初始化/去初始化（HSE 8M×9=72MHz，失败回退 HSI 8MHz）
 *
 * 方案 v3.2 §4.3：HSE 失败 → HSI 8MHz 降级，保证仍可升级。
 * §4.4：跳转前 bl_clock_deinit() 回到 HSI 8MHz，避免 App 时钟初始化异常
 * （App 的 SystemClock_Config 假定从复位态配置 HSE/PLL）。
 */
#ifndef BL_CLOCK_H
#define BL_CLOCK_H

#include "bl_common.h"

/* 上电时钟初始化：优先 72MHz（HSE×9），HSE/PLL/切换任一步失败则回退 HSI 8MHz。
 * 内部记录实际 sysclk/PCLK1，供 USART BRR 与 DWT 换算使用。*/
void bl_clock_init(void);

/* 当前实际频率（bl_clock_init 之后有效）*/
uint32_t bl_clock_sysclk_hz(void);   /* DWT CYCCNT 计数频率 */
uint32_t bl_clock_pclk1_hz(void);    /* USART2 时钟源 */

/* 跳转 App 前去初始化：SYSCLK 切回 HSI → 关 PLL/HSE → Flash 延时恢复 0WS */
void bl_clock_deinit(void);

#endif /* BL_CLOCK_H */
