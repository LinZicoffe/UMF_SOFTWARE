/**
 * @file    bl_time.h
 * @brief   BL 时基（DWT CYCCNT，方案 v3.2 §4.6）
 *
 * 推荐依据：32 位 @72MHz ⇒ 约 59.6s 回绕，远大于 5s 看门狗与全部超时参数，
 * 且无需中断、不依赖 HAL。elapsed_ms 以无符号差值计算，天然处理单次回绕。
 */
#ifndef BL_TIME_H
#define BL_TIME_H

#include "bl_common.h"

void     bl_time_init(void);              /* DEMCR.TRCENA + DWT.CYCCNTENA */
uint32_t bl_time_now(void);               /* 当前 CYCCNT */
uint32_t bl_time_elapsed_ms(uint32_t since); /* now-since 换算毫秒（回绕安全）*/
void     bl_time_delay_ms(uint32_t ms);   /* 忙等（不喂狗，调用方自行安排）*/

#endif /* BL_TIME_H */
