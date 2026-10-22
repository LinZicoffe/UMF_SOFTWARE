#include "display.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <string.h>

#if DISPLAY_ST7789
#include "lcd_init.h"
#include "flow_digits_24x32.h"
#include "main_labels_24.h"

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

static uint8_t font_scale(oled_font_t font)
{
    if (font == OLED_FONT_FLOW_DIGITS || font == OLED_FONT_MAIN_LABELS) return 1U;
    return (uint8_t)(font + 2U);
}

static SSD1306_Font_t lcd_font_data(oled_font_t font)
{
    if (font == OLED_FONT_FLOW_DIGITS) {
        SSD1306_Font_t digits = { FLOW_DIGIT_WIDTH, FLOW_DIGIT_HEIGHT, NULL, NULL };
        return digits;
    }
    if (font == OLED_FONT_MAIN_LABELS) {
        SSD1306_Font_t labels = { MAIN_LABEL_WIDTH, MAIN_LABEL_HEIGHT, NULL, NULL };
        return labels;
    }
    return font == OLED_FONT_SMALL ? Font_7x10 : Font_6x8;
}

static uint8_t main_label_index(char ch)
{
    const char *glyph = strchr(s_main_label_chars, ch);
    return glyph ? (uint8_t)(glyph - s_main_label_chars) : MAIN_LABEL_GLYPH_COUNT;
}

static uint16_t glyph_width(char ch, oled_font_t font)
{
    if (font == OLED_FONT_MAIN_LABELS) {
        uint8_t index = main_label_index(ch);
        if (index < MAIN_LABEL_GLYPH_COUNT) return s_main_label_advances[index];
    }
    return (uint16_t)(lcd_font_data(font).width * font_scale(font));
}

static uint8_t is_superscript_three(const char *text)
{
    return (uint8_t)text[0] == 0xC2U && (uint8_t)text[1] == 0xB3U;
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

static void draw_char(uint16_t x, uint16_t y, char ch, oled_font_t font,
                      uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint8_t row, col, sy, sx;
    SSD1306_Font_t glyph_font = lcd_font_data(font);
    uint8_t columns = glyph_font.width;
    uint16_t width = font == OLED_FONT_MAIN_LABELS ? glyph_width(ch, font)
                                                 : (uint16_t)(glyph_font.width * scale);
    uint16_t height = (uint16_t)(glyph_font.height * scale);
    uint32_t bits;
    uint16_t pixel_y;
    uint8_t glyph_index = 0U;
    if (ch < 32 || ch > 126 || x + width > OLED_WIDTH ||
        y + height > OLED_HEIGHT) return;
    if (font == OLED_FONT_FLOW_DIGITS) {
        if (ch >= '0' && ch <= '9') glyph_index = (uint8_t)(ch - '0');
        else if (ch == '.') glyph_index = 10U;
        else if (ch == '-') glyph_index = 11U;
        else return;
    } else if (font == OLED_FONT_MAIN_LABELS) {
        glyph_index = main_label_index(ch);
        if (glyph_index >= MAIN_LABEL_GLYPH_COUNT) return;
        columns = (uint8_t)width;
    }
    if (s_frame_active) {
        if (y + height <= s_frame_y || y >= s_frame_y + LCD_FRAME_ROWS) return;
    } else {
        LCD_Address_Set(x, y, x + width - 1U, y + height - 1U);
        LCD_StreamBegin();
    }
    for (row = 0; row < glyph_font.height; row++) {
        if (font == OLED_FONT_FLOW_DIGITS) {
            /* 原生数字每行 24 位；其他字体仍按原有倍数展开。 */
            const uint8_t *data = &s_flow_digit_font[glyph_index][row * FLOW_DIGIT_STRIDE];
            bits = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                 | ((uint32_t)data[2] << 8);
        } else if (font == OLED_FONT_MAIN_LABELS) {
            const uint8_t *data = &s_main_label_font[glyph_index][row * MAIN_LABEL_STRIDE];
            bits = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                 | ((uint32_t)data[2] << 8);
        } else {
            bits = (uint32_t)glyph_font.data[((uint8_t)ch - 32U) * glyph_font.height + row] << 16;
        }
        for (sy = 0; sy < scale; sy++) {
            pixel_y = (uint16_t)(y + row * scale + sy);
            if (s_frame_active && (pixel_y < s_frame_y ||
                pixel_y >= s_frame_y + LCD_FRAME_ROWS)) continue;
            for (col = 0; col < columns; col++) {
                uint16_t pixel = (bits & (0x80000000UL >> col)) ? fg : bg;
                for (sx = 0; sx < scale; sx++) {
                    if (s_frame_active)
                        s_frame_pixels[pixel_y - s_frame_y][x + col * scale + sx] = pixel;
                    else
                        stream_color(pixel);
                }
            }
        }
    }
    if (!s_frame_active) LCD_StreamEnd();
}

void OLED_DrawText(uint16_t x, uint16_t y, const char *text, oled_font_t font,
                   uint16_t foreground, uint16_t background)
{
    uint8_t scale = font_scale(font);
    if (!text) return;
    while (*text) {
        uint8_t superscript = font == OLED_FONT_SMALL && is_superscript_three(text);
        uint16_t step = superscript ? Font_7x10.width : glyph_width(*text, font);
        if (x + step > OLED_WIDTH) break;
        /* UTF-8 上标 3 复用现有字模，以原生 7x10 绘制在行顶部。 */
        draw_char(x, y, superscript ? '3' : *text, font, foreground, background,
                  superscript ? 1U : scale);
        text += superscript ? 2 : 1;
        x += step;
    }
}

uint16_t OLED_TextWidth(const char *text, oled_font_t font)
{
    if ((font == OLED_FONT_MAIN_LABELS || font == OLED_FONT_SMALL) && text) {
        uint16_t width = 0U;
        while (*text) {
            if (font == OLED_FONT_SMALL && is_superscript_three(text)) {
                width += Font_7x10.width;
                text += 2;
            } else {
                width += glyph_width(*text++, font);
            }
        }
        return width;
    }
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
