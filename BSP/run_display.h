/**
 * @file    run_display.h
 * @brief   运行显示模块公共 API (S01 主界面 + S02 辅助变量页)
 */
#ifndef RUN_DISPLAY_H
#define RUN_DISPLAY_H

#include "stm32f1xx_hal.h"
#include "bsp_usart.h"

/* 运行页面枚举 */
typedef enum {
    RUN_PAGE_MAIN = 0,
    RUN_PAGE_AUX  = 1,
    RUN_PAGE_COUNT
} run_page_t;

/* 运行显示 INPUT 数据 (主循环组装, const 传入) */
typedef struct {
    const Uart_SendfloatTypeDef *p_flow_rate;
    const Uart_SendfloatTypeDef *p_temperature;
    const Uart_SendfloatTypeDef *p_pressure;
    const uint64_t              *p_cumulative;
    const unsigned char         *p_flow_sum_buf;
    const uint8_t               *p_sum_unit;
    const uint8_t               *p_module_state;
    const uint16_t              *p_dac_value;
    const uint16_t              *p_dac_buf;      /* [2]: DacZeroValue, DacFullValue */
    const char                  *p_flow_unit_str;  /* 流量单位字符串, 由 param_get_flow_unit_str() 填充 */
    const char                  *p_total_unit_str; /* 累积单位字符串, 由 param_get_total_unit_str() 填充 */
} run_display_input_t;

/* 运行显示配置 */
typedef struct {
    uint16_t refresh_period_ms;
} run_display_config_t;

void              run_display_init(const run_display_config_t *p_cfg);
void              run_display_render(const run_display_input_t *p_input);
void              run_display_set_page(run_page_t page);
run_page_t        run_display_get_page(void);
void              run_display_next_page(void);
void              run_display_prev_page(void);

#endif /* RUN_DISPLAY_H */
