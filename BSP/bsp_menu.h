/**
 * @file    bsp_menu.h
 * @brief   菜单系统公共 API — 6模式导航栈 + 密码门控
 * @note    对齐 UMF_HMI_Screen_Design.md v1.0
 */
#ifndef __BSP_MENU_H
#define __BSP_MENU_H

#include "key.h"

typedef struct {
    uint16_t idle_timeout_10ms;  /* 空闲超时 (x10ms), 默认 3000=30s */
} menu_config_t;

typedef struct {
    uint8_t active;       /* 1=菜单激活, 0=运行显示 */
    uint8_t screen_id;    /* 当前屏幕 ID (调试用) */
    uint8_t mode;         /* 0=list,1=numeric,2=enum,3=password,4=readonly,5=confirm */
} menu_status_t;

void     menu_init(const menu_config_t *p_cfg);
uint8_t  menu_process(key_event_t key_evt, menu_status_t *p_out);
void     menu_exit(void);
uint8_t  menu_is_active(void);
void     menu_tick_10ms(void);  /* TIM3 ISR 中调用 */

#endif /* __BSP_MENU_H */
