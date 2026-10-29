#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include "display_config.h"
#if !DISPLAY_ST7789
#include "ssd1306_conf.h"
#endif

#if DISPLAY_ST7789
#define OLED_WIDTH  240U
#define OLED_HEIGHT 240U
#else
#define OLED_WIDTH  128U
#define OLED_HEIGHT 64U
#endif

#define OLED_BLACK  0x0000U
#define OLED_WHITE  0xFFFFU
#define OLED_RED    0xF800U
#define OLED_GREEN  0x07E0U
#define OLED_YELLOW 0xFFE0U
#define OLED_CYAN   0x07FFU
#define OLED_BLUE   0x001FU

/* 工业黑主题，RGB565（红 5 位、绿 6 位、蓝 5 位）。 */
#define OLED_ACCENT 0x565BU
#define OLED_MUTED  0xADD8U
#define OLED_RULE   0x31C8U
#define OLED_SELECT 0x1187U
#define OLED_OK     0x8EF4U
#define OLED_ERROR  0xFB8EU

typedef enum {
    OLED_FONT_SMALL = 0,
    OLED_FONT_MEDIUM,
    OLED_FONT_LARGE,
#if DISPLAY_ST7789
    OLED_FONT_FLOW_DIGITS /* 主界面流量专用，60px 数字；其余为 12/16/32px */
#endif
} oled_font_t;

void OLED_Init(void);
void OLED_Recovery(void);
void OLED_Clear(uint16_t color);
void OLED_FillRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                        uint16_t color);
void OLED_DrawBitmap(uint16_t x, uint16_t y, const uint8_t *bitmap,
                     uint16_t width, uint16_t height, uint16_t color);
void OLED_DrawText(uint16_t x, uint16_t y, const char *text, oled_font_t font,
                   uint16_t foreground, uint16_t background);
uint16_t OLED_TextWidth(const char *text, oled_font_t font);
uint8_t OLED_FontHeight(oled_font_t font);
void OLED_Present(void);
#if DISPLAY_ST7789
/* rotation: 0/1/2/3 分别为顺时针 0°/90°/180°/270°；调用后需重绘整页。 */
void OLED_SetRotation(uint8_t rotation);
/* 按 8 行分段合成整页；回调会重复调用，只能绘制，不能推进业务状态。 */
void OLED_RenderFrame(void (*render)(const void *context), const void *context);
#endif

/* 与原页面的光标式绘制接口保持一致，供 128x64 排版使用。 */
void OLED_SetCursor(uint16_t x, uint16_t y);
char OLED_WriteString(const char *text, oled_font_t font, uint16_t color);

#endif
