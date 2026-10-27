#include "display.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <string.h>

#if DISPLAY_ST7789
#include "lcd_init.h"
#include "industrial_fonts.h"

static uint16_t s_cursor_x;
static uint16_t s_cursor_y;

/* 240x240 RGB565 整屏需要 115200 字节；仅缓存 8 行，复用 3840 字节。 */
#define LCD_FRAME_ROWS 8U
static uint16_t s_frame_pixels[LCD_FRAME_ROWS][OLED_WIDTH];
static uint16_t s_frame_y;
static uint8_t s_frame_active;

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

static const industrial_font_t *lcd_font_data(oled_font_t font)
{
    return &s_industrial_fonts[(uint8_t)font];
}

/* 字符串保留 UTF-8 编码；字模仅补充单位需要的度号、上标 3。 */
static uint8_t next_glyph(const char **text, oled_font_t font)
{
    uint8_t ch = (uint8_t)*(*text)++;
    if (font == OLED_FONT_LARGE || font == OLED_FONT_FLOW_DIGITS) {
        if (ch >= '0' && ch <= '9') return (uint8_t)(ch - '0');
        return ch == '.' ? 10U : 11U;
    }
    if (ch == 0xC2U && (uint8_t)**text == 0xB0U) {
        (*text)++;
        return 95U;
    }
    if (ch == 0xC2U && (uint8_t)**text == 0xB3U) {
        (*text)++;
        return 96U;
    }
    return ch >= 32U && ch <= 126U ? (uint8_t)(ch - 32U) : (uint8_t)('?' - 32);
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
    if (s_frame_active) {
        if (y2 < s_frame_y || y1 >= s_frame_y + LCD_FRAME_ROWS) return;
        if (y1 < s_frame_y) y1 = s_frame_y;
        if (y2 >= s_frame_y + LCD_FRAME_ROWS) y2 = s_frame_y + LCD_FRAME_ROWS - 1U;
        for (y = y1; y <= y2; y++)
            for (x = x1; x <= x2; x++)
                s_frame_pixels[y - s_frame_y][x] = color;
        return;
    }
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

static void draw_char(uint16_t x, uint16_t y, uint8_t index, oled_font_t font,
                      uint16_t fg, uint16_t bg)
{
    const industrial_font_t *data = lcd_font_data(font);
    const industrial_glyph_t *glyph = &data->glyphs[index];
    uint16_t row, col, pixel_y;
    if (x + glyph->advance > OLED_WIDTH || y + data->height > OLED_HEIGHT) return;
    if (s_frame_active) {
        if (y + data->height <= s_frame_y || y >= s_frame_y + LCD_FRAME_ROWS) return;
    } else {
        LCD_Address_Set(x, y, x + glyph->advance - 1U, y + data->height - 1U);
        LCD_StreamBegin();
    }
    for (row = 0; row < data->height; row++) {
        pixel_y = (uint16_t)(y + row);
        if (s_frame_active && (pixel_y < s_frame_y ||
            pixel_y >= s_frame_y + LCD_FRAME_ROWS)) continue;
        for (col = 0; col < glyph->advance; col++) {
            uint16_t pixel = bg;
            if (row >= glyph->top && row < glyph->top + glyph->height) {
                uint16_t bit = (uint16_t)((row - glyph->top) * glyph->advance + col);
                if (data->bitmap[glyph->offset + bit / 8U] & (0x80U >> (bit % 8U)))
                    pixel = fg;
            }
            if (s_frame_active) s_frame_pixels[pixel_y - s_frame_y][x + col] = pixel;
            else stream_color(pixel);
        }
    }
    if (!s_frame_active) LCD_StreamEnd();
}

void OLED_DrawText(uint16_t x, uint16_t y, const char *text, oled_font_t font,
                   uint16_t foreground, uint16_t background)
{
    const industrial_font_t *data = lcd_font_data(font);
    if (!text) return;
    while (*text) {
        uint8_t index = next_glyph(&text, font);
        uint8_t step = data->glyphs[index].advance;
        if (x + step > OLED_WIDTH) break;
        draw_char(x, y, index, font, foreground, background);
        x += step;
    }
}

uint16_t OLED_TextWidth(const char *text, oled_font_t font)
{
    const industrial_font_t *data = lcd_font_data(font);
    uint16_t width = 0U;
    if (text) {
        while (*text) width += data->glyphs[next_glyph(&text, font)].advance;
    }
    return width;
}

uint8_t OLED_FontHeight(oled_font_t font)
{
    return lcd_font_data(font)->height;
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

void OLED_RenderFrame(void (*render)(const void *context), const void *context)
{
    uint16_t row, col;
    LCD_Address_Set(0, 0, OLED_WIDTH - 1U, OLED_HEIGHT - 1U);
    LCD_StreamBegin();
    s_frame_active = 1U;
    for (s_frame_y = 0; s_frame_y < OLED_HEIGHT; s_frame_y += LCD_FRAME_ROWS) {
        memset(s_frame_pixels, 0, sizeof(s_frame_pixels));
        render(context);
        /* 背景和内容在内存中合成后，每个屏幕像素只发送一次。 */
        for (row = 0; row < LCD_FRAME_ROWS; row++)
            for (col = 0; col < OLED_WIDTH; col++)
                stream_color(s_frame_pixels[row][col]);
        IWDG->KR = 0xAAAAU;
    }
    s_frame_active = 0U;
    LCD_StreamEnd();
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
