#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

/* 0: SSD1306 128x64; 1: ST7789 240x240. 每次编译只选择一款屏幕。 */
#define DISPLAY_ST7789 1

#if (DISPLAY_ST7789 != 0) && (DISPLAY_ST7789 != 1)
#error "DISPLAY_ST7789 must be 0 or 1"
#endif

#endif
