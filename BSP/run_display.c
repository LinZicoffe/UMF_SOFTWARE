/**
 * @file    run_display.c
 * @brief   运行显示模块实现 (S01 + S02)
 * @note    所有内部变量和函数均为 static，外部仅通过 run_display.h API 访问
 */
#include "run_display.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "bsp_usart.h"       /* FlowRateValue, FlowTemperature 等 */
#include "bmp.h"             /* 度符号位图 BMP[] */
#include <stdio.h>           /* snprintf */
#include <string.h>          /* strlen */

/* ========== 模块内部状态（全部 static） ========== */
static run_page_t  s_current_page   = RUN_PAGE_MAIN;

/* ========== 数据源引用（extern 全局变量，只读） ========== */
extern uint64_t       Cumulativeflow;
extern unsigned char  strFlowSumBuf[20];
extern unsigned char  strFlowRateBuf[20];
extern unsigned char  strFlowRate_2Buf[10];
extern unsigned char  strFlowTemBuf[20];
extern unsigned char  strFlowPressBuf[20];
extern uint8_t        Sumunit;
extern uint8_t        ModuleState;
extern uint16_t       DacValue;
extern uint16_t       DacValueBuf[2];

/* ========== 内部 static 工具函数 ========== */

/**
 * @brief  DAC 原始值转 4~20mA 电流
 * @param  dac_val DAC 原始值
 * @retval 对应电流值 (mA)
 */
static float dac_to_mA(uint16_t dac_val)
{
    uint16_t zero = DacValueBuf[0];
    uint16_t full = DacValueBuf[1];
    if (full == zero) return 4.0f;
    float ratio = (float)(dac_val - zero) / (float)(full - zero);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    return 4.0f + ratio * 16.0f;
}

/* ========== S01 主界面渲染 ========== */

/**
 * @brief  S01 主界面渲染
 *
 * 像素布局:
 *   Zone A (y=0, H=10px): 状态栏 - 压力 | 温度 | 通信状态
 *   Zone B (y=12, H=36px): 瞬时流量大字 (Font_11x18 双行)
 *   Zone C (y=56, H=8px):  累积流量 (Font_6x8)
 */
static void render_page_main(void)
{
    char buf[16];

    /* ---- Zone A: 状态栏 (y=0, Font_6x8) ---- */
#ifdef SSD1306_INCLUDE_FONT_6x8
    /* 压力 (x=0) */
    snprintf(buf, sizeof(buf), "%.1fKPa", FlowPressure.num);
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString(buf, Font_6x8, White);

    /* 温度 (x=48): 数值 + 度符号位图 + "C" */
    snprintf(buf, sizeof(buf), "%.1f", FlowTemperature.num);
    ssd1306_SetCursor(48, 0);
    ssd1306_WriteString(buf, Font_6x8, White);
    /* 度符号位图 6×8 (来自 bmp.h) */
    ssd1306_DrawBitmap(48 + (uint8_t)strlen(buf) * 6, 0, BMP, 6, 8, White);
    ssd1306_SetCursor(48 + (uint8_t)strlen(buf) * 6 + 6, 0);
    ssd1306_WriteString("C", Font_6x8, White);

    /* 通信状态 (x=90) */
    ssd1306_SetCursor(90, 0);
    ssd1306_WriteString(ModuleState ? "Tx Err" : "Tx ok", Font_6x8, White);
#endif

    /* ---- Zone B: 瞬时流量 (y=12, Font_11x18 双行) ---- */
#ifdef SSD1306_INCLUDE_FONT_11x18
    {
        /* 格式化流量值: 整数部分 + 小数部分合并为 "xxx.x" */
        uint32_t int_part = 0;
        uint32_t frac_part = 0;
        float rate = FlowRateValue.num;
        if (rate < 0.0f) rate = 0.0f;
        int_part  = (uint32_t)rate;
        frac_part = (uint32_t)((rate - (float)int_part) * 10.0f + 0.5f);
        if (frac_part > 9) frac_part = 9;
        snprintf(buf, sizeof(buf), "%lu.%lu", (unsigned long)int_part, (unsigned long)frac_part);

        /* 居中计算: 128 - strlen * 11) / 2 */
        uint8_t len = (uint8_t)strlen(buf);
        uint8_t x_start = (128 - len * 11) / 2;

        /* 双行渲染: y=12 和 y=30 形成视觉加粗效果 */
        ssd1306_SetCursor(x_start, 12);
        ssd1306_WriteString(buf, Font_11x18, White);
        ssd1306_SetCursor(x_start, 30);
        ssd1306_WriteString(buf, Font_11x18, White);
    }
#endif

    /* ---- Zone C: 累积流量 (y=56, Font_6x8) ---- */
#ifdef SSD1306_INCLUDE_FONT_6x8
    ssd1306_SetCursor(0, 56);
    ssd1306_WriteString("TOT ", Font_6x8, White);

    /* 累积值 (strFlowSumBuf 已是 BCD 转换后的字符串) */
    ssd1306_SetCursor(24, 56);
    ssd1306_WriteString((char *)strFlowSumBuf, Font_6x8, White);

    /* 单位 */
    ssd1306_SetCursor(90, 56);
    ssd1306_WriteString(Sumunit ? "m3/h" : "L/h", Font_6x8, White);
#endif
}

/* ========== S02 辅助页渲染 ========== */

/**
 * @brief  S02 辅助变量页渲染
 *
 * Font_6x8, 8 行, 每行 y 间隔 8px, 3 列固定位置布局:
 *   标签 (x=0) | 数值 (x=42) | 单位 (x=90)
 */
static void render_page_aux(void)
{
#ifndef SSD1306_INCLUDE_FONT_6x8
    return;
#else
    char buf[16];

    /* 行 0: 流量 */
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString("Flow:", Font_6x8, White);
    snprintf(buf, sizeof(buf), "%.1f", FlowRateValue.num);
    ssd1306_SetCursor(42, 0);
    ssd1306_WriteString(buf, Font_6x8, White);
    ssd1306_SetCursor(90, 0);
    ssd1306_WriteString(Sumunit ? "m3/h" : "L/h", Font_6x8, White);

    /* 行 1: 流速 (暂无管道截面积参数，占位 0.0) */
    ssd1306_SetCursor(0, 8);
    ssd1306_WriteString("Vel:", Font_6x8, White);
    ssd1306_SetCursor(42, 8);
    ssd1306_WriteString("0.0", Font_6x8, White);
    ssd1306_SetCursor(90, 8);
    ssd1306_WriteString("m/s", Font_6x8, White);

    /* 行 2: 温度 */
    ssd1306_SetCursor(0, 16);
    ssd1306_WriteString("Temp:", Font_6x8, White);
    snprintf(buf, sizeof(buf), "%.1f", FlowTemperature.num);
    ssd1306_SetCursor(42, 16);
    ssd1306_WriteString(buf, Font_6x8, White);
    ssd1306_SetCursor(90, 16);
    ssd1306_WriteString("C", Font_6x8, White);

    /* 行 3: 压力 */
    ssd1306_SetCursor(0, 24);
    ssd1306_WriteString("Press:", Font_6x8, White);
    snprintf(buf, sizeof(buf), "%.1f", FlowPressure.num);
    ssd1306_SetCursor(42, 24);
    ssd1306_WriteString(buf, Font_6x8, White);
    ssd1306_SetCursor(90, 24);
    ssd1306_WriteString("KPa", Font_6x8, White);

    /* 行 4: DAC 电流输出 */
    ssd1306_SetCursor(0, 32);
    ssd1306_WriteString("Cur:", Font_6x8, White);
    snprintf(buf, sizeof(buf), "%.1f", dac_to_mA(DacValue));
    ssd1306_SetCursor(42, 32);
    ssd1306_WriteString(buf, Font_6x8, White);
    ssd1306_SetCursor(90, 32);
    ssd1306_WriteString("mA", Font_6x8, White);

    /* 行 5: 频率 (暂无直接变量，占位) */
    ssd1306_SetCursor(0, 40);
    ssd1306_WriteString("Freq:", Font_6x8, White);
    ssd1306_SetCursor(42, 40);
    ssd1306_WriteString("0.0", Font_6x8, White);
    ssd1306_SetCursor(90, 40);
    ssd1306_WriteString("Hz", Font_6x8, White);

    /* 行 6: 通信状态 */
    ssd1306_SetCursor(0, 48);
    ssd1306_WriteString("Comm:", Font_6x8, White);
    ssd1306_SetCursor(42, 48);
    ssd1306_WriteString(ModuleState ? "Tx Err" : "Tx ok", Font_6x8, White);

    /* 行 7: 累积流量 */
    ssd1306_SetCursor(0, 56);
    ssd1306_WriteString("TOT:", Font_6x8, White);
    ssd1306_SetCursor(42, 56);
    ssd1306_WriteString((char *)strFlowSumBuf, Font_6x8, White);
    ssd1306_SetCursor(90, 56);
    ssd1306_WriteString(Sumunit ? "m3" : "L", Font_6x8, White);
#endif
}

/* ========== Public API 实现 ========== */

void run_display_init(const run_display_config_t *p_cfg)
{
    /* 配置参数预留，当前仅使用默认值 */
    (void)p_cfg;
    s_current_page = RUN_PAGE_MAIN;

    /* 初始化 SSD1306 */
    ssd1306_Init();
    ssd1306_Fill(Black);
    ssd1306_UpdateScreen();
}

void run_display_render(void)
{
    ssd1306_Fill(Black);  /* 清帧缓冲 */

    switch (s_current_page) {
        case RUN_PAGE_MAIN: render_page_main(); break;
        case RUN_PAGE_AUX:  render_page_aux();  break;
        default:            render_page_main(); break;
    }
    /* 注意: 此处不调用 ssd1306_UpdateScreen()，由主循环统一刷新 */
}

void run_display_set_page(run_page_t page)
{
    if (page < RUN_PAGE_COUNT) {
        s_current_page = page;
    }
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
    s_current_page = (s_current_page == 0) ?
                      (run_page_t)(RUN_PAGE_COUNT - 1) : (run_page_t)(s_current_page - 1);
}
