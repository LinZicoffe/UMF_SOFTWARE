/**
 * @file    run_display.c
 * @brief   运行显示实现 (S01 + S02)
 * @note    不依赖 extern 全局变量, 所有数据通过 run_display_input_t 传入
 */
#include "run_display.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "bmp.h"
#include <stdio.h>
#include <string.h>

/* 内部状态 — 全部 static */
static run_page_t  s_current_page   = RUN_PAGE_MAIN;

/* 工具函数 — static */
static float dac_to_mA(uint16_t dac_val, const uint16_t *p_dac_buf)
{
    uint16_t zero = p_dac_buf[0];
    uint16_t full = p_dac_buf[1];
    float ratio;
    if (full == zero) return 4.0f;
    ratio = (float)(dac_val - zero) / (float)(full - zero);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    return 4.0f + ratio * 16.0f;
}

/* ---- S01 主界面 ---- */
static void render_page_main(const run_display_input_t *p_in)
{
    char buf[16];
    float rate;
    uint8_t len, x_start;

#ifdef SSD1306_INCLUDE_FONT_6x8
    /* Zone A: 状态栏 (y=0) — 压力 */
    snprintf(buf, sizeof(buf), "%.1fKPa", p_in->p_pressure->num);
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString(buf, Font_6x8, White);

    /* 温度 + 度符号 */
    snprintf(buf, sizeof(buf), "%.1f", p_in->p_temperature->num);
    ssd1306_SetCursor(48, 0);
    ssd1306_WriteString(buf, Font_6x8, White);
    ssd1306_DrawBitmap(48 + (uint8_t)strlen(buf) * 6, 0, BMP, 6, 8, White);
    ssd1306_SetCursor(48 + (uint8_t)strlen(buf) * 6 + 6, 0);
    ssd1306_WriteString("C", Font_6x8, White);

    /* 通信状态 */
    ssd1306_SetCursor(90, 0);
    ssd1306_WriteString((char *)(*(p_in->p_module_state) ? "Tx Err" : "Tx ok"), Font_6x8, White);
#endif

#ifdef SSD1306_INCLUDE_FONT_11x18
    /* Zone B: 瞬时流量 (y=14, 双行加粗) */
    /* 4 位有效数字自适应小数位 */
    rate = p_in->p_flow_rate->num;
    if (rate < 0.0f) rate = 0.0f;
    if (rate >= 1000.0f) {
        snprintf(buf, sizeof(buf), "%.0f", rate);       /* >=1000: 无小数, 如 1234 */
    } else if (rate >= 100.0f) {
        snprintf(buf, sizeof(buf), "%.1f", rate);       /* 100~999: 1 位小数, 如 123.4 */
    } else if (rate >= 10.0f) {
        snprintf(buf, sizeof(buf), "%.2f", rate);       /* 10~99: 2 位小数, 如 12.34 */
    } else {
        snprintf(buf, sizeof(buf), "%.3f", rate);       /* 0~9: 3 位小数, 如 1.234 */
    }

    len = (uint8_t)strlen(buf);
    x_start = (uint8_t)((128 - len * 11) / 2);
    ssd1306_SetCursor(x_start, 22);
    ssd1306_WriteString(buf, Font_11x18, White);
#endif

#ifdef SSD1306_INCLUDE_FONT_6x8
    /* Zone C: 累积流量 (y=56) */
    ssd1306_SetCursor(0, 56);
    ssd1306_WriteString("TOT ", Font_6x8, White);
    ssd1306_SetCursor(30, 56);
    ssd1306_WriteString((char *)p_in->p_flow_sum_buf, Font_6x8, White);
    ssd1306_SetCursor(96, 56);
    ssd1306_WriteString((char *)p_in->p_total_unit_str, Font_6x8, White);
#endif
}

/* ---- S02 辅助页 ---- */
static void render_page_aux(const run_display_input_t *p_in)
{
    char buf[16];
    float fval;

#ifdef SSD1306_INCLUDE_FONT_6x8
    /* y=0: Flow */
    ssd1306_SetCursor(0, 0);  ssd1306_WriteString("Flow:", Font_6x8, White);
    snprintf(buf, sizeof(buf), "%.1f", p_in->p_flow_rate->num);
    ssd1306_SetCursor(42, 0); ssd1306_WriteString(buf, Font_6x8, White);
    ssd1306_SetCursor(90, 0);
    ssd1306_WriteString((char *)p_in->p_flow_unit_str, Font_6x8, White);

    /* y=8: Vel (占位) */
    ssd1306_SetCursor(0, 8);  ssd1306_WriteString("Vel:", Font_6x8, White);
    ssd1306_SetCursor(42, 8); ssd1306_WriteString("0.00", Font_6x8, White);
    ssd1306_SetCursor(90, 8); ssd1306_WriteString("m/s", Font_6x8, White);

    /* y=16: Temp */
    ssd1306_SetCursor(0, 16); ssd1306_WriteString("Temp:", Font_6x8, White);
    snprintf(buf, sizeof(buf), "%.1f", p_in->p_temperature->num);
    ssd1306_SetCursor(42, 16); ssd1306_WriteString(buf, Font_6x8, White);
    ssd1306_SetCursor(90, 16); ssd1306_WriteString("C", Font_6x8, White);

    /* y=24: Press */
    ssd1306_SetCursor(0, 24); ssd1306_WriteString("Press:", Font_6x8, White);
    snprintf(buf, sizeof(buf), "%.1f", p_in->p_pressure->num);
    ssd1306_SetCursor(42, 24); ssd1306_WriteString(buf, Font_6x8, White);
    ssd1306_SetCursor(90, 24); ssd1306_WriteString("KPa", Font_6x8, White);

    /* y=32: Cur (4~20mA) */
    ssd1306_SetCursor(0, 32); ssd1306_WriteString("Cur:", Font_6x8, White);
    fval = dac_to_mA(*(p_in->p_dac_value), p_in->p_dac_buf);
    snprintf(buf, sizeof(buf), "%.1f", fval);
    ssd1306_SetCursor(42, 32); ssd1306_WriteString(buf, Font_6x8, White);
    ssd1306_SetCursor(90, 32); ssd1306_WriteString("mA", Font_6x8, White);

    /* y=40: Freq (占位) */
    ssd1306_SetCursor(0, 40); ssd1306_WriteString("Freq:", Font_6x8, White);
    ssd1306_SetCursor(42, 40); ssd1306_WriteString("0.0", Font_6x8, White);
    ssd1306_SetCursor(90, 40); ssd1306_WriteString("Hz", Font_6x8, White);

    /* y=48: Comm */
    ssd1306_SetCursor(0, 48); ssd1306_WriteString("Comm:", Font_6x8, White);
    ssd1306_SetCursor(42, 48);
    ssd1306_WriteString((char *)(*(p_in->p_module_state) ? "Tx Err" : "Tx ok"), Font_6x8, White);

    /* y=56: TOT */
    ssd1306_SetCursor(0, 56); ssd1306_WriteString("TOT:", Font_6x8, White);
    ssd1306_SetCursor(42, 56); ssd1306_WriteString((char *)p_in->p_flow_sum_buf, Font_6x8, White);
    ssd1306_SetCursor(96, 56);
    ssd1306_WriteString((char *)p_in->p_total_unit_str, Font_6x8, White);
#endif
}

/* ---- Public API ---- */

void run_display_init(const run_display_config_t *p_cfg)
{
    (void)p_cfg;
    s_current_page = RUN_PAGE_MAIN;
    ssd1306_Init();
    ssd1306_Fill(Black);
    ssd1306_UpdateScreen();
}

void run_display_render(const run_display_input_t *p_input)
{
    ssd1306_Fill(Black);
    switch (s_current_page) {
    case RUN_PAGE_MAIN: render_page_main(p_input); break;
    case RUN_PAGE_AUX:  render_page_aux(p_input);  break;
    default:            render_page_main(p_input); break;
    }
    /* 不调用 ssd1306_UpdateScreen(), 由主循环统一刷新 */
}

void run_display_set_page(run_page_t page)
{
    if (page < RUN_PAGE_COUNT) s_current_page = page;
}

run_page_t run_display_get_page(void)
{
    return s_current_page;
}

void run_display_next_page(void)
{
    s_current_page = (run_page_t)((s_current_page + 1) % RUN_PAGE_COUNT);
}

void run_display_prev_page(void)
{
    s_current_page = (run_page_t)((s_current_page + RUN_PAGE_COUNT - 1) % RUN_PAGE_COUNT);
}
