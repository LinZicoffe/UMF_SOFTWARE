/**
 * @file    bl_jump.h
 * @brief   App 跳转（方案 v3.2 §4.4 —— 顺序不可颠倒的固化模板）
 */
#ifndef BL_JUMP_H
#define BL_JUMP_H

#include "bl_common.h"

/* 跳转到 App。前置：固件头层 1 已通过；本函数内部再次校验向量表。
 * 成功后不返回；向量表非法返回 BL_ERR_ADDR（调用方停留升级模式）。
 * 模板红线（方案附录 B）：
 *   __enable_irq() 不可省略——PRIMASK 不随跳转清零，App 自身从不使能中断；
 *   SCB->ICSR 清 SysTick/PendSV 挂起位——ICER/ICPR 覆盖不到异常 14/15；
 *   看门狗先放宽 5s（覆盖 App 启动期）、时钟 deinit 回 HSI、
 *   VTOR 先于 MSP 设置。跳转后 IWDG 以 5s 周期运行，App 须自行
 *   早喂狗/重配（App 侧前置改造清单第 5 条）。*/
bl_status_t bl_jump_to_app(void);

#endif /* BL_JUMP_H */
