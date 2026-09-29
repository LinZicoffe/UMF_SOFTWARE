/**
 * @file    run_display.c
 * @brief   运行显示实现 (S01 + S02)
 * @note    不依赖 extern 全局变量, 所有数据通过 run_display_input_t 传入
 */
#include "run_display.h"
#include "display.h"
#include "bmp.h"
#include "ftoa.h"
#include <string.h>

/* 内部状态 — 全部 static */
static run_page_t  s_current_page   = RUN_PAGE_MAIN;
#if DISPLAY_ST7789
static uint8_t s_display_dirty = 1U;
#endif

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

#if DISPLAY_ST7789

static char s_rate_text[24];
static uint16_t s_rate_x;
static oled_font_t s_rate_font;
static char s_temp_text[24];
static char s_total_text[32];
static char s_aux_text[8][32];
static run_page_t s_drawn_page = RUN_PAGE_COUNT;
static uint16_t s_flow_unit_width;
static uint16_t s_total_unit_width;
static uint8_t s_background_prepared;
static uint8_t s_reveal_after_render;

static void erase_text(uint16_t x, uint16_t y, uint16_t width, oled_font_t font)
{
    if (width == 0U) return;
    OLED_FillRectangle(x, y, x + width - 1U,
                       y + OLED_FontHeight(font) - 1U, OLED_BLACK);
}

static void erase_main_page(void)
{
    erase_text(8, 8, s_flow_unit_width, OLED_FONT_SMALL);
    erase_text(8, 26, OLED_TextWidth(s_temp_text, OLED_FONT_SMALL), OLED_FONT_SMALL);
    erase_text(190, 8, OLED_TextWidth(s_aux_text[0], OLED_FONT_SMALL), OLED_FONT_SMALL);
    erase_text(8, 49, OLED_TextWidth("FLOW", OLED_FONT_MEDIUM), OLED_FONT_MEDIUM);
    erase_text(s_rate_x, 91, OLED_TextWidth(s_rate_text, s_rate_font), s_rate_font);
    erase_text(8, 153, OLED_TextWidth("TOTAL", OLED_FONT_MEDIUM), OLED_FONT_MEDIUM);
    erase_text(8, 184, OLED_TextWidth(s_total_text, OLED_FONT_SMALL), OLED_FONT_SMALL);
    erase_text(8, 213, s_total_unit_width, OLED_FONT_SMALL);
}

static void erase_aux_page(void)
{
    static const char * const labels[7] = {
        "Flow", "Vel", "Temp", "Press", "Cur", "Freq", "Comm"
    };
    uint8_t i;
    for (i = 0; i < 7U; i++) {
        uint16_t y = (uint16_t)(5U + i * 28U);
        erase_text(8, y, OLED_TextWidth(labels[i], OLED_FONT_SMALL), OLED_FONT_SMALL);
        erase_text(80, y, OLED_TextWidth(s_aux_text[i], OLED_FONT_SMALL), OLED_FONT_SMALL);
    }
    erase_text(8, 202, OLED_TextWidth("TOTAL", OLED_FONT_SMALL), OLED_FONT_SMALL);
    erase_text(8, 220, OLED_TextWidth(s_total_text, OLED_FONT_SMALL), OLED_FONT_SMALL);
}

static void draw_field(uint16_t x, uint16_t y, uint16_t width,
                       char *previous, const char *value, oled_font_t font,
                       uint16_t color)
{
    if (!s_display_dirty && strcmp(previous, value) == 0) return;
    OLED_FillRectangle(x, y, x + width - 1U,
                       y + OLED_FontHeight(font) - 1U, OLED_BLACK);
    OLED_DrawText(x, y, value, font, color, OLED_BLACK);
    strcpy(previous, value);
}

static void render_page_main(const run_display_input_t *p_in)
{
    char buf[32];
    float rate = p_in->p_flow_rate->num;
    float temp = p_in->p_temperature->num;
    uint16_t x;
    if (s_display_dirty) {
        if (!s_background_prepared) OLED_Clear(OLED_BLACK);
        s_flow_unit_width = OLED_TextWidth(p_in->p_flow_unit_str, OLED_FONT_SMALL);
        s_total_unit_width = OLED_TextWidth(p_in->p_total_unit_str, OLED_FONT_SMALL);
        OLED_DrawText(8, 8, p_in->p_flow_unit_str, OLED_FONT_SMALL,
                      OLED_CYAN, OLED_BLACK);
        OLED_DrawText(8, 49, "FLOW", OLED_FONT_MEDIUM,
                      OLED_CYAN, OLED_BLACK);
        OLED_DrawText(8, 153, "TOTAL", OLED_FONT_MEDIUM,
                      OLED_CYAN, OLED_BLACK);
        OLED_DrawText(8, 213, p_in->p_total_unit_str, OLED_FONT_SMALL,
                      OLED_WHITE, OLED_BLACK);
    }
    if (temp > 999.9f) temp = 999.9f;
    if (temp < -99.9f) temp = -99.9f;
    ftoa(temp, 1, buf, sizeof(buf));
    strcat(buf, "C");
    draw_field(8, 26, 160, s_temp_text, buf, OLED_FONT_SMALL, OLED_WHITE);
    draw_field(190, 8, 48, s_aux_text[0],
               *(p_in->p_module_state) ? "ER" : "OK", OLED_FONT_SMALL,
               *(p_in->p_module_state) ? OLED_RED : OLED_GREEN);
    if (rate < 0.0f) rate = 0.0f;
    ftoa(rate, rate >= 1000.0f ? 0U : rate >= 100.0f ? 1U :
          rate >= 10.0f ? 2U : 3U, buf, sizeof(buf));
    if (s_display_dirty || strcmp(s_rate_text, buf) != 0) {
        oled_font_t rate_font = OLED_TextWidth(buf, OLED_FONT_LARGE) <= 232U
                              ? OLED_FONT_LARGE : OLED_FONT_MEDIUM;
        size_t i;
        char glyph[2] = { 0, 0 };
        x = OLED_TextWidth(buf, rate_font);
        x = x < OLED_WIDTH ? (uint16_t)((OLED_WIDTH - x) / 2U) : 0U;
        if (s_display_dirty || x != s_rate_x || rate_font != s_rate_font) {
            OLED_FillRectangle(8, 83, 239, 133, OLED_BLACK);
            OLED_DrawText(x, 91, buf, rate_font, OLED_WHITE, OLED_BLACK);
        } else {
            uint16_t glyph_width = OLED_TextWidth("0", rate_font);
            for (i = 0; buf[i] != '\0'; i++) {
                if (buf[i] == s_rate_text[i]) continue;
                glyph[0] = buf[i];
                OLED_DrawText(x + (uint16_t)i * glyph_width, 91, glyph,
                              rate_font, OLED_WHITE, OLED_BLACK);
            }
        }
        strcpy(s_rate_text, buf);
        s_rate_x = x;
        s_rate_font = rate_font;
    }
    strcpy(buf, (const char *)p_in->p_flow_sum_buf);
    draw_field(8, 184, 232, s_total_text, buf, OLED_FONT_SMALL, OLED_YELLOW);
}

static void render_page_aux(const run_display_input_t *p_in)
{
    char buf[32];
    float current = dac_to_mA(*(p_in->p_dac_value), p_in->p_dac_buf);
    static const char * const labels[7] = {
        "Flow", "Vel", "Temp", "Press", "Cur", "Freq", "Comm"
    };
    uint8_t i;
    if (s_display_dirty) {
        if (!s_background_prepared) OLED_Clear(OLED_BLACK);
        for (i = 0; i < 7U; i++)
            OLED_DrawText(8, 5U + i * 28U, labels[i], OLED_FONT_SMALL,
                          OLED_CYAN, OLED_BLACK);
        OLED_DrawText(8, 202, "TOTAL", OLED_FONT_SMALL,
                      OLED_CYAN, OLED_BLACK);
    }
    ftoa(p_in->p_flow_rate->num, 1, buf, sizeof(buf));
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1U);
    strncat(buf, p_in->p_flow_unit_str, sizeof(buf) - strlen(buf) - 1U);
    draw_field(80, 5, 160, s_aux_text[0], buf, OLED_FONT_SMALL, OLED_WHITE);
    draw_field(80, 33, 160, s_aux_text[1], "0.00 m/s", OLED_FONT_SMALL, OLED_WHITE);
    ftoa(p_in->p_temperature->num, 1, buf, sizeof(buf));
    strcat(buf, " C");
    draw_field(80, 61, 160, s_aux_text[2], buf, OLED_FONT_SMALL, OLED_WHITE);
    ftoa(p_in->p_pressure->num, 1, buf, sizeof(buf));
    strcat(buf, " KPa");
    draw_field(80, 89, 160, s_aux_text[3], buf, OLED_FONT_SMALL, OLED_WHITE);
    ftoa(current, 1, buf, sizeof(buf));
    strcat(buf, " mA");
    draw_field(80, 117, 160, s_aux_text[4], buf, OLED_FONT_SMALL, OLED_WHITE);
    draw_field(80, 145, 160, s_aux_text[5], "0.0 Hz", OLED_FONT_SMALL, OLED_WHITE);
    draw_field(80, 173, 160, s_aux_text[6],
               *(p_in->p_module_state) ? "Tx Err" : "Tx OK", OLED_FONT_SMALL,
               *(p_in->p_module_state) ? OLED_RED : OLED_GREEN);
    strcpy(buf, (const char *)p_in->p_flow_sum_buf);
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1U);
    strncat(buf, p_in->p_total_unit_str, sizeof(buf) - strlen(buf) - 1U);
    draw_field(8, 220, 232, s_total_text, buf, OLED_FONT_SMALL, OLED_YELLOW);
}

#else

/* ---- S01 主界面 ---- */
static void render_page_main(const run_display_input_t *p_in)
{
    char buf[24];
    float rate;
    uint8_t len, x_start;
    float temp;

#ifdef SSD1306_INCLUDE_FONT_7x10
    /* Zone A: 状态栏 (y=0, OLED_FONT_MEDIUM)
     * 左侧显示当前瞬时流量单位，温度紧随其后，通信状态右对齐。 */
    OLED_SetCursor(0, 0);
    OLED_WriteString((char *)p_in->p_flow_unit_str, OLED_FONT_MEDIUM, OLED_WHITE);
    x_start = (uint8_t)(strlen(p_in->p_flow_unit_str) * 7 + 8);

    /* 温度 + 6x8 度符号位图（字母和数字均为 7x10 字体） */
    temp = p_in->p_temperature->num;
    if (temp > 999.9f)  temp = 999.9f;
    if (temp < -99.9f)  temp = -99.9f;
    ftoa(temp, 1, buf, sizeof(buf));
    OLED_SetCursor(x_start, 0);
    OLED_WriteString(buf, OLED_FONT_MEDIUM, OLED_WHITE);
    x_start = (uint8_t)(x_start + strlen(buf) * 7);
    OLED_DrawBitmap(x_start, 1, BMP, 6, 8, OLED_WHITE);
    OLED_SetCursor((uint8_t)(x_start + 6), 0);
    OLED_WriteString("C", OLED_FONT_MEDIUM, OLED_WHITE);

    /* 通信状态固定右对齐：2 char x 7px */
    OLED_SetCursor(114, 0);
    OLED_WriteString((char *)(*(p_in->p_module_state) ? "ER" : "OK"), OLED_FONT_MEDIUM, OLED_WHITE);
#endif

#ifdef SSD1306_INCLUDE_FONT_16x26
    /* Zone B: 瞬时流量 (16×26 大字, 垂直居中于状态栏与累积栏之间) */
    rate = p_in->p_flow_rate->num;
    if (rate < 0.0f) rate = 0.0f;
    if (rate >= 1000.0f) {
        ftoa(rate, 0, buf, sizeof(buf));
    } else if (rate >= 100.0f) {
        ftoa(rate, 1, buf, sizeof(buf));
    } else if (rate >= 10.0f) {
        ftoa(rate, 2, buf, sizeof(buf));
    } else {
        ftoa(rate, 3, buf, sizeof(buf));
    }
    len = (uint8_t)strlen(buf);
    x_start = (uint8_t)((128 - len * 16) / 2);
    OLED_SetCursor(x_start, 19);
    OLED_WriteString(buf, OLED_FONT_LARGE, OLED_WHITE);
#elif defined(SSD1306_INCLUDE_FONT_11x18)
    /* Zone B: 瞬时流量 (11×18, 4位有效数字自适应小数位) */
    rate = p_in->p_flow_rate->num;
    if (rate < 0.0f) rate = 0.0f;
    if (rate >= 1000.0f) {
        ftoa(rate, 0, buf, sizeof(buf));
    } else if (rate >= 100.0f) {
        ftoa(rate, 1, buf, sizeof(buf));
    } else if (rate >= 10.0f) {
        ftoa(rate, 2, buf, sizeof(buf));
    } else {
        ftoa(rate, 3, buf, sizeof(buf));
    }
    len = (uint8_t)strlen(buf);
    x_start = (uint8_t)((128 - len * 11) / 2);
    OLED_SetCursor(x_start, 22);
    OLED_WriteString(buf, OLED_FONT_LARGE, OLED_WHITE);
#endif

#ifdef SSD1306_INCLUDE_FONT_7x10
    /* Zone C: 累积流量 (y=54, OLED_FONT_MEDIUM)
     * 主界面跳过累积量最高位："TOT "(28px) + 12位流量串(84px)
     * + 最长2位单位(14px) = 126px，原始累积量数据保持不变。 */
    OLED_SetCursor(0, 54);
    OLED_WriteString("TOT ", OLED_FONT_MEDIUM, OLED_WHITE);
    OLED_SetCursor(28, 54);
    OLED_WriteString((char *)p_in->p_flow_sum_buf + 1, OLED_FONT_MEDIUM, OLED_WHITE);
    OLED_SetCursor(112, 54);
    OLED_WriteString((char *)p_in->p_total_unit_str, OLED_FONT_MEDIUM, OLED_WHITE);
#endif
}

/* ---- S02 辅助页 ---- */
static void render_page_aux(const run_display_input_t *p_in)
{
    char buf[24];
    float fval;

#ifdef SSD1306_INCLUDE_FONT_6x8
    /* y=0: Flow */
    OLED_SetCursor(0, 0);  OLED_WriteString("Flow:", OLED_FONT_SMALL, OLED_WHITE);
    ftoa(p_in->p_flow_rate->num, 1, buf, sizeof(buf));
    OLED_SetCursor(42, 0); OLED_WriteString(buf, OLED_FONT_SMALL, OLED_WHITE);
    OLED_SetCursor(90, 0);
    OLED_WriteString((char *)p_in->p_flow_unit_str, OLED_FONT_SMALL, OLED_WHITE);

    /* y=8: Vel (占位) */
    OLED_SetCursor(0, 8);  OLED_WriteString("Vel:", OLED_FONT_SMALL, OLED_WHITE);
    OLED_SetCursor(42, 8); OLED_WriteString("0.00", OLED_FONT_SMALL, OLED_WHITE);
    OLED_SetCursor(90, 8); OLED_WriteString("m/s", OLED_FONT_SMALL, OLED_WHITE);

    /* y=16: Temp */
    OLED_SetCursor(0, 16); OLED_WriteString("Temp:", OLED_FONT_SMALL, OLED_WHITE);
    ftoa(p_in->p_temperature->num, 1, buf, sizeof(buf));
    OLED_SetCursor(42, 16); OLED_WriteString(buf, OLED_FONT_SMALL, OLED_WHITE);
    OLED_SetCursor(90, 16); OLED_WriteString("C", OLED_FONT_SMALL, OLED_WHITE);

    /* y=24: Press */
    OLED_SetCursor(0, 24); OLED_WriteString("Press:", OLED_FONT_SMALL, OLED_WHITE);
    ftoa(p_in->p_pressure->num, 1, buf, sizeof(buf));
    OLED_SetCursor(42, 24); OLED_WriteString(buf, OLED_FONT_SMALL, OLED_WHITE);
    OLED_SetCursor(90, 24); OLED_WriteString("KPa", OLED_FONT_SMALL, OLED_WHITE);

    /* y=32: Cur (4~20mA) */
    OLED_SetCursor(0, 32); OLED_WriteString("Cur:", OLED_FONT_SMALL, OLED_WHITE);
    fval = dac_to_mA(*(p_in->p_dac_value), p_in->p_dac_buf);
    ftoa(fval, 1, buf, sizeof(buf));
    OLED_SetCursor(42, 32); OLED_WriteString(buf, OLED_FONT_SMALL, OLED_WHITE);
    OLED_SetCursor(90, 32); OLED_WriteString("mA", OLED_FONT_SMALL, OLED_WHITE);

    /* y=40: Freq (占位) */
    OLED_SetCursor(0, 40); OLED_WriteString("Freq:", OLED_FONT_SMALL, OLED_WHITE);
    OLED_SetCursor(42, 40); OLED_WriteString("0.0", OLED_FONT_SMALL, OLED_WHITE);
    OLED_SetCursor(90, 40); OLED_WriteString("Hz", OLED_FONT_SMALL, OLED_WHITE);

    /* y=48: Comm */
    OLED_SetCursor(0, 48); OLED_WriteString("Comm:", OLED_FONT_SMALL, OLED_WHITE);
    OLED_SetCursor(42, 48);
    OLED_WriteString((char *)(*(p_in->p_module_state) ? "Tx Err" : "Tx ok"), OLED_FONT_SMALL, OLED_WHITE);

    /* y=56: TOT — 同样修正布局覆盖问题
     * 流量串13char×6px=78px，从x=30到x=107；单位从x=108起，不再覆盖小数位 */
    OLED_SetCursor(0, 56); OLED_WriteString("TOT:", OLED_FONT_SMALL, OLED_WHITE);
    OLED_SetCursor(30, 56); OLED_WriteString((char *)p_in->p_flow_sum_buf, OLED_FONT_SMALL, OLED_WHITE);
    OLED_SetCursor(108, 56);
    OLED_WriteString((char *)p_in->p_total_unit_str, OLED_FONT_SMALL, OLED_WHITE);
#endif
}

#endif /* DISPLAY_ST7789 */

/* ---- Public API ---- */

void run_display_init(const run_display_config_t *p_cfg)
{
    (void)p_cfg;
    s_current_page = RUN_PAGE_MAIN;
    OLED_Init();
    OLED_Clear(OLED_BLACK);
    OLED_Present();
#if DISPLAY_ST7789
    s_display_dirty = 1U;
    s_drawn_page = RUN_PAGE_COUNT;
    s_background_prepared = 0U;
    s_reveal_after_render = 0U;
#endif
}

void run_display_render(const run_display_input_t *p_input)
{
#if !DISPLAY_ST7789
    OLED_Clear(OLED_BLACK);
#else
    if (s_drawn_page != s_current_page) s_display_dirty = 1U;
#endif
    switch (s_current_page) {
    case RUN_PAGE_MAIN: render_page_main(p_input); break;
    case RUN_PAGE_AUX:  render_page_aux(p_input);  break;
    default:            render_page_main(p_input); break;
    }
    /* 由主循环统一调用 OLED_Present()。 */
#if DISPLAY_ST7789
    s_display_dirty = 0U;
    s_drawn_page = s_current_page;
    s_background_prepared = 0U;
    if (s_reveal_after_render) {
        OLED_SetDisplayEnabled(1U);
        s_reveal_after_render = 0U;
    }
#endif
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

void run_display_invalidate(void)
{
#if DISPLAY_ST7789
    s_display_dirty = 1U;
#endif
}

void run_display_erase_visible_page(void)
{
#if DISPLAY_ST7789
    if (s_drawn_page == RUN_PAGE_MAIN) erase_main_page();
    else if (s_drawn_page == RUN_PAGE_AUX) erase_aux_page();
    s_drawn_page = RUN_PAGE_COUNT;
    s_display_dirty = 1U;
#endif
}

void run_display_prepare_after_menu(void)
{
#if DISPLAY_ST7789
    s_drawn_page = RUN_PAGE_COUNT;
    s_display_dirty = 1U;
    s_background_prepared = 1U;
    s_reveal_after_render = 1U;
#endif
}
