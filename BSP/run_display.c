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
// ---- 绘制字段 ---- 
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

static const char *main_unit_text(const char *unit)
{
    if (strcmp(unit, "m3/h") == 0) return "m\xC2\xB3/h";
    if (strcmp(unit, "m3") == 0) return "m\xC2\xB3";
    return unit;
}
static void draw_centered(uint16_t y, const char *text, oled_font_t font,
                          uint16_t color)
{
    uint16_t width = OLED_TextWidth(text, font);
    OLED_DrawText((OLED_WIDTH - width) / 2U, y, text, font, color, OLED_BLACK);
}

/* S01：状态栏、瞬时流量、完整累积量。 */
static void render_page_main(const run_display_input_t *p_in)
{
    char buf[32];
    float rate = p_in->p_flow_rate->num;
    float temp = p_in->p_temperature->num;
    if (s_display_dirty) {
        OLED_FillRectangle(14, 39, 225, 39, OLED_RULE);
        draw_centered(50, "Flow rate", OLED_FONT_MEDIUM, OLED_ACCENT);
        draw_centered(141, main_unit_text(p_in->p_flow_unit_str),
                       OLED_FONT_MEDIUM, OLED_MUTED);
        OLED_FillRectangle(14, 170, 225, 170, OLED_RULE);
        OLED_DrawText(14, 183, "Total", OLED_FONT_SMALL, OLED_MUTED, OLED_BLACK);
        OLED_DrawText(225U - OLED_TextWidth(main_unit_text(p_in->p_total_unit_str),
                      OLED_FONT_SMALL), 183, main_unit_text(p_in->p_total_unit_str),
                      OLED_FONT_SMALL, OLED_MUTED, OLED_BLACK);
    }
    if (temp > 999.9f) temp = 999.9f;
    if (temp < -99.9f) temp = -99.9f;
    ftoa(temp, 1, buf, sizeof(buf));
    strcat(buf, " \xC2\xB0" "C");
    draw_field(14, 11, 135, s_temp_text, buf, OLED_FONT_MEDIUM, OLED_MUTED);
    draw_field(185, 11, 41, s_aux_text[0],
               *(p_in->p_module_state) ? "ER" : "OK", OLED_FONT_MEDIUM,
               *(p_in->p_module_state) ? OLED_ERROR : OLED_OK);
    if (rate < 0.0f) rate = 0.0f;
    ftoa(rate, rate >= 1000.0f ? 0U : rate >= 100.0f ? 1U :
          rate >= 10.0f ? 2U : 3U, buf, sizeof(buf));
    if (s_display_dirty || strcmp(s_rate_text, buf) != 0) {
        oled_font_t font = OLED_FONT_FLOW_DIGITS;
        uint16_t width = OLED_TextWidth(buf, font);
        uint16_t x = (OLED_WIDTH - width) / 2U;
        uint8_t same_cells = !s_display_dirty && x == s_rate_x && font == s_rate_font;
        size_t i;
        char glyph[2] = { 0, 0 };
        /* 比例字体即使总宽相同，各字符起点也可能不同。 */
        for (i = 0; same_cells && buf[i] != '\0'; i++) {
            char old_glyph[2] = { s_rate_text[i], 0 };
            glyph[0] = buf[i];
            if (OLED_TextWidth(glyph, font) != OLED_TextWidth(old_glyph, font))
                same_cells = 0U;
        }
        if (!same_cells) {
            OLED_FillRectangle(8, 74, 231, 137, OLED_BLACK);
            OLED_DrawText(x, 74, buf, font, OLED_WHITE, OLED_BLACK);
        } else {
            for (i = 0; buf[i] != '\0'; i++) {
                glyph[0] = buf[i];
                if (buf[i] != s_rate_text[i])
                    OLED_DrawText(x, 74, glyph, font, OLED_WHITE, OLED_BLACK);
                x += OLED_TextWidth(glyph, font);
            }
        }
        strcpy(s_rate_text, buf);
        s_rate_x = (OLED_WIDTH - width) / 2U;
        s_rate_font = font;
    }
    draw_field(14, 204, 212, s_total_text, (const char *)p_in->p_flow_sum_buf,
               OLED_FONT_MEDIUM, OLED_WHITE);
}

/* S02：七项实时数据使用同一套字体和列对齐。 */
static void render_page_aux(const run_display_input_t *p_in)
{
    char buf[32];
    float current = dac_to_mA(*(p_in->p_dac_value), p_in->p_dac_buf);
    static const char * const labels[7] = {
        "Flow", "Velocity", "Temp", "Pressure", "Current", "Freq", "Comm"
    };
    uint8_t i;
    if (s_display_dirty) {
        OLED_DrawText(14, 11, "Live data", OLED_FONT_MEDIUM, OLED_MUTED, OLED_BLACK);
        OLED_FillRectangle(14, 39, 225, 39, OLED_RULE);
        for (i = 0; i < 7U; i++)
            OLED_DrawText(14, 48U + i * 21U, labels[i], OLED_FONT_SMALL,
                          OLED_MUTED, OLED_BLACK);
        OLED_FillRectangle(14, 196, 225, 196, OLED_RULE);
        OLED_DrawText(14, 201, "Total", OLED_FONT_SMALL, OLED_MUTED, OLED_BLACK);
    }
    ftoa(p_in->p_flow_rate->num, 1, buf, sizeof(buf));
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1U);
    strncat(buf, main_unit_text(p_in->p_flow_unit_str), sizeof(buf) - strlen(buf) - 1U);
    draw_field(90, 48, 136, s_aux_text[0], buf, OLED_FONT_SMALL, OLED_WHITE);
    draw_field(90, 69, 136, s_aux_text[1], "0.00 m/s", OLED_FONT_SMALL, OLED_WHITE);
    ftoa(p_in->p_temperature->num, 1, buf, sizeof(buf));
    strcat(buf, " \xC2\xB0" "C");
    draw_field(90, 90, 136, s_aux_text[2], buf, OLED_FONT_SMALL, OLED_WHITE);
    ftoa(p_in->p_pressure->num, 1, buf, sizeof(buf));
    strcat(buf, " KPa");
    draw_field(90, 111, 136, s_aux_text[3], buf, OLED_FONT_SMALL, OLED_WHITE);
    ftoa(current, 1, buf, sizeof(buf));
    strcat(buf, " mA");
    draw_field(90, 132, 136, s_aux_text[4], buf, OLED_FONT_SMALL, OLED_WHITE);
    draw_field(90, 153, 136, s_aux_text[5], "0.0 Hz", OLED_FONT_SMALL, OLED_WHITE);
    draw_field(90, 174, 136, s_aux_text[6],
               *(p_in->p_module_state) ? "Tx Err" : "Tx OK", OLED_FONT_SMALL,
               *(p_in->p_module_state) ? OLED_ERROR : OLED_OK);
    strcpy(buf, (const char *)p_in->p_flow_sum_buf);
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1U);
    strncat(buf, main_unit_text(p_in->p_total_unit_str), sizeof(buf) - strlen(buf) - 1U);
    draw_field(14, 220, 212, s_total_text, buf, OLED_FONT_SMALL, OLED_WHITE);
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
// ---- 渲染回调 ----
static void render_run_frame(const void *context)
{
    const run_display_input_t *p_input = context;
    switch (s_current_page) {
    case RUN_PAGE_MAIN: render_page_main(p_input); break;
    case RUN_PAGE_AUX:  render_page_aux(p_input);  break;
    default:           render_page_main(p_input); break;
    }
}

/* ---- Public API ---- */

// ---- 初始化显示器 ----
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
#endif
}

void run_display_render(const run_display_input_t *p_input)
{
#if !DISPLAY_ST7789
    OLED_Clear(OLED_BLACK);
    render_run_frame(p_input);
#else
    if (s_drawn_page != s_current_page) s_display_dirty = 1U;
    if (s_display_dirty)
        OLED_RenderFrame(render_run_frame, p_input);
    else
        render_run_frame(p_input);
#endif
    /* 由主循环统一调用 OLED_Present()。 */
#if DISPLAY_ST7789
    s_display_dirty = 0U;
    s_drawn_page = s_current_page;
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
