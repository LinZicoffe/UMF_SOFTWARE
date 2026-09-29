#include "display.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <string.h>

#if DISPLAY_ST7789
#include "lcd_init.h"

static uint16_t s_cursor_x;
static uint16_t s_cursor_y;

static void LCD_StreamBegin(void)
{
    OLED_DC_GPIO_Port->BSRR = OLED_DC_Pin;
    OLED_CS_GPIO_Port->BRR = OLED_CS_Pin;
}

static void LCD_StreamWrite8(uint8_t dat)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        OLED_CLK_GPIO_Port->BRR = OLED_CLK_Pin;
        if (dat & 0x80U)
            OLED_SDA_GPIO_Port->BSRR = OLED_SDA_Pin;
        else
            OLED_SDA_GPIO_Port->BRR = OLED_SDA_Pin;
        __NOP();
        OLED_CLK_GPIO_Port->BSRR = OLED_CLK_Pin;
        dat <<= 1;
    }
}

static void LCD_StreamEnd(void)
{
    OLED_CS_GPIO_Port->BSRR = OLED_CS_Pin;
}

static uint8_t font_scale(oled_font_t font)
{
    return (uint8_t)(font + 2U);
}

static SSD1306_Font_t lcd_font_data(oled_font_t font)
{
    return font == OLED_FONT_SMALL ? Font_7x10 : Font_6x8;
}

static void stream_color(uint16_t color)
{
    LCD_StreamWrite8((uint8_t)(color >> 8));
    LCD_StreamWrite8((uint8_t)color);
}

void OLED_Init(void)
{
    LCD_Init();
    s_cursor_x = 0;
    s_cursor_y = 0;
}

void OLED_Recovery(void)
{
    LCD_WR_REG(0x36);
    LCD_WR_DATA8(0x00);
    LCD_WR_REG(0x3A);
    LCD_WR_DATA8(0x05);
    LCD_WR_REG(0x21);
    LCD_WR_REG(0x29);
}

void OLED_FillRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                        uint16_t color)
{
    uint16_t y;
    uint16_t x;
    if (x1 > x2 || y1 > y2 || x1 >= OLED_WIDTH || y1 >= OLED_HEIGHT) return;
    if (x2 >= OLED_WIDTH) x2 = OLED_WIDTH - 1U;
    if (y2 >= OLED_HEIGHT) y2 = OLED_HEIGHT - 1U;
    LCD_Address_Set(x1, y1, x2, y2);
    LCD_StreamBegin();
    for (y = y1; y <= y2; y++) {
        for (x = x1; x <= x2; x++) stream_color(color);
        /* 大面积填充可能超过看门狗周期。 */
        IWDG->KR = 0xAAAAU;
    }
    LCD_StreamEnd();
}

void OLED_Clear(uint16_t color)
{
    OLED_FillRectangle(0, 0, OLED_WIDTH - 1U, OLED_HEIGHT - 1U, color);
}

static void draw_char(uint16_t x, uint16_t y, char ch, oled_font_t font,
                      uint16_t fg, uint16_t bg)
{
    uint8_t row, col, sy, sx;
    uint8_t scale = font_scale(font);
    SSD1306_Font_t glyph_font = lcd_font_data(font);
    uint16_t width = (uint16_t)(glyph_font.width * scale);
    uint16_t height = (uint16_t)(glyph_font.height * scale);
    uint16_t bits;
    if (ch < 32 || ch > 126 || x + width > OLED_WIDTH ||
        y + height > OLED_HEIGHT) return;
    LCD_Address_Set(x, y, x + width - 1U, y + height - 1U);
    LCD_StreamBegin();
    for (row = 0; row < glyph_font.height; row++) {
        bits = glyph_font.data[((uint8_t)ch - 32U) * glyph_font.height + row];
        for (sy = 0; sy < scale; sy++) {
            for (col = 0; col < glyph_font.width; col++) {
                uint16_t pixel = (bits & (0x8000U >> col)) ? fg : bg;
                for (sx = 0; sx < scale; sx++) stream_color(pixel);
            }
        }
    }
    LCD_StreamEnd();
}

void OLED_DrawText(uint16_t x, uint16_t y, const char *text, oled_font_t font,
                   uint16_t foreground, uint16_t background)
{
    uint8_t scale = font_scale(font);
    uint16_t step = (uint16_t)(lcd_font_data(font).width * scale);
    if (!text) return;
    while (*text && x + step <= OLED_WIDTH) {
        draw_char(x, y, *text++, font, foreground, background);
        x += step;
    }
}

uint16_t OLED_TextWidth(const char *text, oled_font_t font)
{
    return text ? (uint16_t)(strlen(text) * lcd_font_data(font).width * font_scale(font)) : 0U;
}

uint8_t OLED_FontHeight(oled_font_t font)
{
    return (uint8_t)(lcd_font_data(font).height * font_scale(font));
}

void OLED_DrawBitmap(uint16_t x, uint16_t y, const uint8_t *bitmap,
                     uint16_t width, uint16_t height, uint16_t color)
{
    uint16_t row, col;
    if (!bitmap) return;
    for (row = 0; row < height; row++) {
        for (col = 0; col < width; col++) {
            if (bitmap[row * ((width + 7U) / 8U) + col / 8U] & (0x80U >> (col % 8U)))
                OLED_FillRectangle(x + col, y + row, x + col, y + row, color);
        }
    }
}

void OLED_Present(void) { }

void OLED_SetDisplayEnabled(uint8_t enabled)
{
    LCD_WR_REG(enabled ? 0x29U : 0x28U);
}

void OLED_SetCursor(uint16_t x, uint16_t y)
{
    s_cursor_x = x;
    s_cursor_y = y;
}

char OLED_WriteString(const char *text, oled_font_t font, uint16_t color)
{
    OLED_DrawText(s_cursor_x, s_cursor_y, text, font, color, OLED_BLACK);
    s_cursor_x += OLED_TextWidth(text, font);
    return 0;
}

#else

static SSD1306_Font_t select_font(oled_font_t font)
{
    if (font == OLED_FONT_SMALL) return Font_6x8;
    if (font == OLED_FONT_LARGE) return Font_11x18;
    return Font_7x10;
}

static SSD1306_COLOR mono_color(uint16_t color)
{
    return color == OLED_BLACK ? Black : White;
}

void OLED_Init(void) { ssd1306_Init(); }
void OLED_Recovery(void) { ssd1306_RecoveryInit(); }
void OLED_Clear(uint16_t color) { ssd1306_Fill(mono_color(color)); }
void OLED_Present(void) { ssd1306_UpdateScreen(); }

void OLED_FillRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                        uint16_t color)
{
    ssd1306_FillRectangle((uint8_t)x1, (uint8_t)y1, (uint8_t)x2, (uint8_t)y2,
                          mono_color(color));
}

void OLED_DrawBitmap(uint16_t x, uint16_t y, const uint8_t *bitmap,
                     uint16_t width, uint16_t height, uint16_t color)
{
    ssd1306_DrawBitmap((uint8_t)x, (uint8_t)y, bitmap, (uint8_t)width,
                       (uint8_t)height, mono_color(color));
}

void OLED_DrawText(uint16_t x, uint16_t y, const char *text, oled_font_t font,
                   uint16_t foreground, uint16_t background)
{
    (void)background;
    ssd1306_SetCursor((uint8_t)x, (uint8_t)y);
    ssd1306_WriteString((char *)text, select_font(font), mono_color(foreground));
}

uint16_t OLED_TextWidth(const char *text, oled_font_t font)
{
    return text ? (uint16_t)(strlen(text) * select_font(font).width) : 0U;
}

uint8_t OLED_FontHeight(oled_font_t font)
{
    return select_font(font).height;
}

void OLED_SetCursor(uint16_t x, uint16_t y)
{
    ssd1306_SetCursor((uint8_t)x, (uint8_t)y);
}

char OLED_WriteString(const char *text, oled_font_t font, uint16_t color)
{
    return ssd1306_WriteString((char *)text, select_font(font), mono_color(color));
}

#endif
