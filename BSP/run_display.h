/**
 * @file    run_display.h
 * @brief   运行显示模块公共 API (S01 主界面 + S02 辅助变量页)
 */
#ifndef RUN_DISPLAY_H
#define RUN_DISPLAY_H

#include "stm32f1xx_hal.h"

/* 运行页面枚举 */
typedef enum {
    RUN_PAGE_MAIN = 0,    /* S01: 瞬时/累积流量主界面 */
    RUN_PAGE_AUX  = 1,    /* S02: 辅助变量页 */
    RUN_PAGE_COUNT
} run_page_t;

/* 运行显示配置 */
typedef struct {
    uint16_t refresh_period_ms;     /* 刷新周期 (ms), 默认 200 */
} run_display_config_t;

/* ===== Public API ===== */

/* 初始化运行显示模块 */
void              run_display_init(const run_display_config_t *p_cfg);

/* 渲染当前页到帧缓冲 (仅写 SSD1306_Buffer, 不调用 ssd1306_UpdateScreen) */
void              run_display_render(void);

/* 页面控制 */
void              run_display_set_page(run_page_t page);
run_page_t        run_display_get_page(void);
void              run_display_next_page(void);   /* K_DOWN: 翻到下一页 */
void              run_display_prev_page(void);   /* K_UP: 翻到上一页 */

#endif /* RUN_DISPLAY_H */
