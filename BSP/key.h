/**
 * @file    key.h
 * @brief   按键驱动 — 3 键 + 组合键检测
 * @note    面板实际接线 (非 CubeMX 命名):
 *          K1 = K_MOV  (PC15): 向下选择
 *          K2 = K_SUB  (PA0):  确认/进入
 *          K3 = K_ADD  (PA11): 向上选择
 *          K1+K2 (PC15+PA0):  返回上一级
 *          K1+K2+K3:          返回主界面
 */
#ifndef __BSP_KEY_H
#define __BSP_KEY_H

#include "main.h"

/* 按键事件枚举 */
typedef enum {
    KEY_NONE      = 0,  /* 无事件 */
    KEY_UP        = 1,  /* K3 (PA11):  向上选择 */
    KEY_DOWN      = 2,  /* K1 (PC15):  向下选择 */
    KEY_ENTER     = 3,  /* K2 (PA0):   确认/进入 */
    KEY_BACK      = 4,  /* K1+K2:      返回上一级 */
    KEY_HOME      = 6   /* K1+K2+K3:   返回主界面 */
} key_event_t;

/**
 * @brief  按键初始化（清除内部状态）
 */
void key_init(void);

/**
 * @brief  10ms 定时扫描（在 TIM3 回调中调用）
 * @note   内部完成消抖 + 组合键判定
 */
void key_scan_10ms(void);

/**
 * @brief  获取按键事件（非阻塞，主循环调用）
 * @retval key_event_t  KEY_NONE 表示无事件
 */
key_event_t key_get_event(void);

#endif /* __BSP_KEY_H */
