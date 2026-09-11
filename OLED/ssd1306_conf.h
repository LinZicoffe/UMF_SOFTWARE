/**
 * @file    ssd1306_conf.h
 * @brief   afiskon/stm32-ssd1306 库配置文件 (UMF 硬件适配)
 * @note    在 ssd1306.h 之前被 include
 */
#ifndef SSD1306_CONF_H
#define SSD1306_CONF_H

/* 使用 SPI 模式 */
#define SSD1306_USE_SPI

/* MCU 系列 */
#define STM32F1

/* 显示分辨率 */
#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64

/* UMF 硬件引脚映射 */
#define SSD1306_CS_Port    GPIOA
#define SSD1306_CS_Pin     GPIO_PIN_7
#define SSD1306_DC_Port    GPIOA
#define SSD1306_DC_Pin     GPIO_PIN_6
#define SSD1306_Reset_Port GPIOA
#define SSD1306_Reset_Pin  GPIO_PIN_5

/* 字体选择 (Flash 预算)
 * 重要诊断 [测试 E]: 启用 Font_16x26 + bsp_menu 完整代码后, 字体表被
 *   推过 64KB Flash 边界, 读取返回 0xFF -> 字符渲染为实心白方块.
 * 解决方案: 临时禁用 Font_16x26, run_display.c 已有 #elif Font_11x18 fallback. */
#define SSD1306_INCLUDE_FONT_6x8       /* 状态栏 + 辅助页 ≈1.1KB */
#define SSD1306_INCLUDE_FONT_7x10      /* 主界面状态栏 + 累积量 ≈1.9KB */
#define SSD1306_INCLUDE_FONT_11x18     /* 瞬时流量大字 ≈3.4KB */
/* #define SSD1306_INCLUDE_FONT_16x26 */  /* 暂时禁用 ≈5KB - Flash 越界根因 */

/* bit-bang SPI 自定义标志 — ssd1306.c 中用条件编译选择 bit-bang 路径 */
#define SSD1306_BITBANG_SPI

/* 画弧功能 — 需要 <math.h>, 在无 FPU 芯片上增加 4~8KB Flash。默认禁用 */
/* #define SSD1306_ENABLE_ARC */

#endif /* SSD1306_CONF_H */
