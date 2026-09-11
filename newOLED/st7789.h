/**
 * @file    st7789.h
 * @brief   ST7789P3 TFT LCD 驱动 (ENH-TV0200A032, 240x320 IPS, 4-line SPI)
 * @note    无帧缓冲直接写屏 — 通过窗口+像素流刷新, 节省 RAM
 */

#ifndef ST7789_H
#define ST7789_H

#include <stddef.h>
#include <stdint.h>
#include "st7789_conf.h"

#include "stm32f1xx_hal.h"

/* ── RGB565 颜色定义 ────────────────────────────────── */
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_RED         0xF800
#define COLOR_GREEN       0x07E0
#define COLOR_BLUE        0x001F
#define COLOR_YELLOW      0xFFE0
#define COLOR_CYAN        0x07FF
#define COLOR_MAGENTA     0xF81F
#define COLOR_ORANGE      0xFD20
#define COLOR_GRAY        0x8410
#define COLOR_DARKGRAY    0x4208
#define COLOR_LIGHTGRAY   0xC618

/* 从 R,G,B (0~255) 合成 RGB565 */
#define RGB565(r, g, b)  (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

/* ── 字体结构 ──────────────────────────────────────── */
typedef struct {
    const uint8_t  width;       /* 字符宽度 (像素) */
    const uint8_t  height;      /* 字符高度 (像素) */
    const uint16_t *data;       /* 字模数据数组指针 */
} ST7789_Font_t;

/* ── 初始化 / 电源管理 ─────────────────────────────── */
void ST7789_Init(void);
void ST7789_SleepIn(void);
void ST7789_SleepOut(void);
void ST7789_DisplayOn(void);
void ST7789_DisplayOff(void);
void ST7789_InvertColors(uint8_t invert);

/* ── 基础绘图 ──────────────────────────────────────── */
void ST7789_Fill(uint16_t color);
void ST7789_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                     uint16_t color);
void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void ST7789_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                     uint16_t color);
void ST7789_DrawRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                     uint16_t color);
void ST7789_DrawBitmap(uint16_t x, uint16_t y, const uint8_t *bmp,
                        uint16_t w, uint16_t h, uint16_t color);

/* ── 文本输出 ──────────────────────────────────────── */
void ST7789_SetCursor(uint16_t x, uint16_t y);
char ST7789_WriteChar(char ch, ST7789_Font_t font, uint16_t fg, uint16_t bg);
char ST7789_WriteString(const char *str, ST7789_Font_t font,
                         uint16_t fg, uint16_t bg);

/* ── 滚动 ──────────────────────────────────────────── */
void ST7789_SetScrollArea(uint16_t top, uint16_t scroll, uint16_t bottom);
void ST7789_Scroll(uint16_t pixels);

#endif /* ST7789_H */
