/**
 * @file    st7789_conf.h
 * @brief   ST7789P3 驱动硬件配置 (ENH-TV0200A032 2.0寸 240x320 IPS TFT)
 * @note    在 st7789.h 之前被 include
 */
#ifndef ST7789_CONF_H
#define ST7789_CONF_H

/* 使用 bit-bang SPI 模式 */
#define ST7789_USE_BITBANG_SPI

/* MCU 系列 */
#define STM32F1

/* 显示分辨率 */
#define ST7789_WIDTH  240
#define ST7789_HEIGHT 320

/* UMF 硬件引脚映射 — 与 SSD1306 引脚兼容, 可按实际接线修改 */
#define ST7789_CS_Port      GPIOA
#define ST7789_CS_Pin       GPIO_PIN_7
#define ST7789_DC_Port      GPIOA
#define ST7789_DC_Pin       GPIO_PIN_6
#define ST7789_Reset_Port   GPIOA
#define ST7789_Reset_Pin    GPIO_PIN_5
#define ST7789_SCL_Port     GPIOB
#define ST7789_SCL_Pin      GPIO_PIN_0
#define ST7789_SDA_Port     GPIOA
#define ST7789_SDA_Pin      GPIO_PIN_4

/* 字体选择 (Flash 预算控制) */
#define ST7789_INCLUDE_FONT_6x8
#define ST7789_INCLUDE_FONT_11x18
/* #define ST7789_INCLUDE_FONT_7x10 */

#endif /* ST7789_CONF_H */
