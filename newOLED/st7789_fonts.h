/**
 * @file    st7789_fonts.h
 * @brief   ST7789 驱动字体声明 (RGB565 彩色屏用)
 */

#ifndef ST7789_FONTS_H
#define ST7789_FONTS_H

#include "st7789.h"

#ifdef ST7789_INCLUDE_FONT_6x8
extern const ST7789_Font_t Font_6x8;
#endif
#ifdef ST7789_INCLUDE_FONT_7x10
extern const ST7789_Font_t Font_7x10;
#endif
#ifdef ST7789_INCLUDE_FONT_11x18
extern const ST7789_Font_t Font_11x18;
#endif

#endif /* ST7789_FONTS_H */
