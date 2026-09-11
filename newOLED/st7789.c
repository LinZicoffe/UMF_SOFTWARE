/**
 * @file    st7789.c
 * @brief   ST7789P3 TFT LCD 驱动实现 (ENH-TV0200A032, 240x320 IPS, 4-line SPI)
 * @note    无帧缓冲 — 通过窗口(CASET/RASET) + 像素流(RAMWR) 直接写屏
 *          适配 STM32F103C8T6 (20KB RAM, 无法容纳 240x320x2=153.6KB 帧缓冲)
 */

#include "st7789.h"
#include "st7789_fonts.h"

/* ── ST7789P3 命令定义 ─────────────────────────────── */
#define CMD_NOP      0x00    /* 空操作 */
#define CMD_SWRESET  0x01    /* 软件复位 */
#define CMD_SLPIN    0x10    /* 进入睡眠 */
#define CMD_SLPOUT   0x11    /* 退出睡眠 */
#define CMD_NORON    0x13    /* 正常显示模式 */
#define CMD_INVOFF   0x20    /* 关闭反色 */
#define CMD_INVON    0x21    /* 开启反色 */
#define CMD_DISPOFF  0x28    /* 关闭显示 */
#define CMD_DISPON   0x29    /* 开启显示 */
#define CMD_CASET    0x2A    /* 列地址设置 (X) */
#define CMD_RASET    0x2B    /* 行地址设置 (Y) */
#define CMD_RAMWR    0x2C    /* 写显存 */
#define CMD_RAMRD    0x2E    /* 读显存 */
#define CMD_MADCTL   0x36    /* 内存数据访问控制 */
#define CMD_COLMOD   0x3A    /* 像素格式 */
#define CMD_PORCTRL  0xB2    /* 电源控制 1 */
#define CMD_GCTRL    0xB7    /* 门控控制 */
#define CMD_VCOMS    0xBB    /* VCOM 设置 */
#define CMD_LCMCTRL  0xC0    /* LCD 电源控制 */
#define CMD_IDSET    0xC1    /* ID 设置 */
#define CMD_VDVVRHEN 0xC2    /* VDV/VRH 命令使能 */
#define CMD_VRHS     0xC3    /* VRH 设置 */
#define CMD_VDVS     0xC4    /* VDV 设置 */
#define CMD_VCMOFSET 0xC5    /* VCOM 偏移 */
#define CMD_FRCTRL2  0xC6    /* 帧率控制 */
#define CMD_CABCCTRL 0xC7    /* CABC 控制 */
#define CMD_REGSEL1  0xC8    /* 寄存器选择 1 */
#define CMD_REGSEL2  0xCA    /* 寄存器选择 2 */
#define CMD_PWMFRSEL 0xCC    /* PWM 频率选择 */
#define CMD_PWCTRL1  0xD0    /* 电源控制 1 */
#define CMD_VAPVAN   0xD6    /* VAP/VAN 设置 */
#define CMD_PVGAM    0xE0    /* 正极性伽马校正 */
#define CMD_NVGAM    0xE1    /* 负极性伽马校正 */

/* MADCTL 位定义 */
#define MADCTL_MY  0x80    /* 行地址翻转 */
#define MADCTL_MX  0x40    /* 列地址翻转 */
#define MADCTL_MV  0x20    /* 行列交换 */
#define MADCTL_ML  0x10    /* 垂直刷新顺序 */
#define MADCTL_RGB 0x00    /* RGB 像素顺序 */
#define MADCTL_BGR 0x08    /* BGR 像素顺序 */

/* ── 模块内部状态 ──────────────────────────────────── */
static uint16_t s_cursor_x;
static uint16_t s_cursor_y;
static uint8_t  s_initialized;

/* ── bit-bang SPI 底层 ──────────────────────────────── */

/* GPIO 宏 — 适配 HAL 库 */
#define PIN_HIGH(port, pin)  HAL_GPIO_WritePin((port), (pin), GPIO_PIN_SET)
#define PIN_LOW(port, pin)   HAL_GPIO_WritePin((port), (pin), GPIO_PIN_RESET)

static void bitbang_spi_write(uint8_t byte)
{
    /* MSB first, 8-bit, MODE 0 (CPOL=0, CPHA=0) */
    for (int8_t i = 7; i >= 0; i--) {
        PIN_LOW(ST7789_SCL_Port, ST7789_SCL_Pin);
        if (byte & (1 << i)) {
            PIN_HIGH(ST7789_SDA_Port, ST7789_SDA_Pin);
        } else {
            PIN_LOW(ST7789_SDA_Port, ST7789_SDA_Pin);
        }
        __NOP();  /* 数据建立时间 */
        PIN_HIGH(ST7789_SCL_Port, ST7789_SCL_Pin);
        __NOP();  /* 数据保持时间 */
    }
}

/* ── SPI 命令/数据发送 ─────────────────────────────── */

static void ST7789_WriteCommand(uint8_t cmd)
{
    PIN_LOW(ST7789_DC_Port, ST7789_DC_Pin);   /* DC=0: 命令 */
    PIN_LOW(ST7789_CS_Port, ST7789_CS_Pin);
    bitbang_spi_write(cmd);
    PIN_HIGH(ST7789_CS_Port, ST7789_CS_Pin);
}

static void ST7789_WriteData8(uint8_t data)
{
    PIN_HIGH(ST7789_DC_Port, ST7789_DC_Pin);  /* DC=1: 数据 */
    PIN_LOW(ST7789_CS_Port, ST7789_CS_Pin);
    bitbang_spi_write(data);
    PIN_HIGH(ST7789_CS_Port, ST7789_CS_Pin);
}

static void ST7789_WriteData16(uint16_t data)
{
    PIN_HIGH(ST7789_DC_Port, ST7789_DC_Pin);
    PIN_LOW(ST7789_CS_Port, ST7789_CS_Pin);
    bitbang_spi_write((uint8_t)(data >> 8));
    bitbang_spi_write((uint8_t)(data & 0xFF));
    PIN_HIGH(ST7789_CS_Port, ST7789_CS_Pin);
}

/* 连续写入多个 16-bit 数据 (用于填充区域, CS 保持低) */
static void ST7789_WriteData16Burst(uint16_t data, uint32_t count)
{
    uint8_t hi = (uint8_t)(data >> 8);
    uint8_t lo = (uint8_t)(data & 0xFF);

    PIN_HIGH(ST7789_DC_Port, ST7789_DC_Pin);
    PIN_LOW(ST7789_CS_Port, ST7789_CS_Pin);
    for (uint32_t i = 0; i < count; i++) {
        bitbang_spi_write(hi);
        bitbang_spi_write(lo);
    }
    PIN_HIGH(ST7789_CS_Port, ST7789_CS_Pin);
}

/* ── 窗口设置 ──────────────────────────────────────── */

static void ST7789_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    /* 列地址 (X) */
    ST7789_WriteCommand(CMD_CASET);
    ST7789_WriteData16(x0);
    ST7789_WriteData16(x1);

    /* 行地址 (Y) */
    ST7789_WriteCommand(CMD_RASET);
    ST7789_WriteData16(y0);
    ST7789_WriteData16(y1);

    /* 准备写入显存 */
    ST7789_WriteCommand(CMD_RAMWR);
}

/* ── 硬件复位 ──────────────────────────────────────── */

static void ST7789_HardwareReset(void)
{
    PIN_HIGH(ST7789_Reset_Port, ST7789_Reset_Pin);
    HAL_Delay(10);
    PIN_LOW(ST7789_Reset_Port, ST7789_Reset_Pin);
    HAL_Delay(10);
    PIN_HIGH(ST7789_Reset_Port, ST7789_Reset_Pin);
    HAL_Delay(120);
}

/* ── 发送初始化命令序列 (按规格书) ──────────────────── */

static void ST7789_SendInitCommands(void)
{
    /* 退出睡眠 */
    ST7789_WriteCommand(CMD_SLPOUT);
    HAL_Delay(120);

    /* PORCTRL (0xB2) — 电源控制 */
    ST7789_WriteCommand(CMD_PORCTRL);
    ST7789_WriteData8(0x0C);
    ST7789_WriteData8(0x0C);
    ST7789_WriteData8(0x00);
    ST7789_WriteData8(0x33);
    ST7789_WriteData8(0x33);

    /* TEON (0x35) — 帧同步, 关闭 */
    ST7789_WriteCommand(0x35);
    ST7789_WriteData8(0x00);

    /* MADCTL (0x36) — 内存访问控制: RGB 顺序, 无翻转 */
    ST7789_WriteCommand(CMD_MADCTL);
    ST7789_WriteData8(MADCTL_RGB);

    /* GCTRL (0xB7) — 门控: VGH=13.65V, VGL=-10.43V */
    ST7789_WriteCommand(CMD_GCTRL);
    ST7789_WriteData8(0x35);

    /* VCOMS (0xBB) — VCOM: 1.175V */
    ST7789_WriteCommand(CMD_VCOMS);
    ST7789_WriteData8(0x34);

    /* VDVVRHEN (0xC2) */
    ST7789_WriteCommand(CMD_VDVVRHEN);
    ST7789_WriteData8(0x01);

    /* VRHS (0xC3) — VRH: 4.5V */
    ST7789_WriteCommand(CMD_VRHS);
    ST7789_WriteData8(0x13);

    /* VDVS (0xC4) — VDV */
    ST7789_WriteCommand(CMD_VDVS);
    ST7789_WriteData8(0x20);

    /* FRCTRL2 (0xC6) — 帧率: 60Hz */
    ST7789_WriteCommand(CMD_FRCTRL2);
    ST7789_WriteData8(0x0F);

    /* PWCTRL1 (0xD0) — 电源控制: AVDD=6.8V, AVCL=-4.8V, VDDS=2.3V */
    ST7789_WriteCommand(CMD_PWCTRL1);
    ST7789_WriteData8(0xA4);
    ST7789_WriteData8(0xA1);

    /* VAPVAN (0xD6) */
    ST7789_WriteCommand(CMD_VAPVAN);
    ST7789_WriteData8(0xA1);

    /* 正极性伽马校正 (0xE0) */
    ST7789_WriteCommand(CMD_PVGAM);
    ST7789_WriteData8(0xD0);
    ST7789_WriteData8(0x0A);
    ST7789_WriteData8(0x10);
    ST7789_WriteData8(0x0C);
    ST7789_WriteData8(0x0C);
    ST7789_WriteData8(0x18);
    ST7789_WriteData8(0x35);
    ST7789_WriteData8(0x43);
    ST7789_WriteData8(0x4D);
    ST7789_WriteData8(0x39);
    ST7789_WriteData8(0x13);
    ST7789_WriteData8(0x13);
    ST7789_WriteData8(0x2D);
    ST7789_WriteData8(0x34);

    /* 负极性伽马校正 (0xE1) */
    ST7789_WriteCommand(CMD_NVGAM);
    ST7789_WriteData8(0xD0);
    ST7789_WriteData8(0x05);
    ST7789_WriteData8(0x0B);
    ST7789_WriteData8(0x06);
    ST7789_WriteData8(0x05);
    ST7789_WriteData8(0x02);
    ST7789_WriteData8(0x35);
    ST7789_WriteData8(0x43);
    ST7789_WriteData8(0x4D);
    ST7789_WriteData8(0x16);
    ST7789_WriteData8(0x15);
    ST7789_WriteData8(0x15);
    ST7789_WriteData8(0x2E);
    ST7789_WriteData8(0x32);

    /* 像素格式: 16-bit (RGB565) */
    ST7789_WriteCommand(CMD_COLMOD);
    ST7789_WriteData8(0x05);

    /* 开启反色 (规格书要求) */
    ST7789_WriteCommand(CMD_INVON);

    /* 开启显示 */
    ST7789_WriteCommand(CMD_DISPON);
}

/* ══════════════════════════════════════════════════════
 *  Public API 实现
 * ══════════════════════════════════════════════════════ */

void ST7789_Init(void)
{
    ST7789_HardwareReset();
    ST7789_SendInitCommands();
    HAL_Delay(20);

    /* 清屏为黑色 */
    ST7789_Fill(COLOR_BLACK);

    s_cursor_x = 0;
    s_cursor_y = 0;
    s_initialized = 1;
}

void ST7789_SleepIn(void)
{
    ST7789_WriteCommand(CMD_SLPIN);
    HAL_Delay(5);
}

void ST7789_SleepOut(void)
{
    ST7789_WriteCommand(CMD_SLPOUT);
    HAL_Delay(120);
}

void ST7789_DisplayOn(void)
{
    ST7789_WriteCommand(CMD_DISPON);
}

void ST7789_DisplayOff(void)
{
    ST7789_WriteCommand(CMD_DISPOFF);
}

void ST7789_InvertColors(uint8_t invert)
{
    ST7789_WriteCommand(invert ? CMD_INVON : CMD_INVOFF);
}

/* ── 绘图函数 ──────────────────────────────────────── */

void ST7789_Fill(uint16_t color)
{
    ST7789_FillRect(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1, color);
}

void ST7789_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                     uint16_t color)
{
    /* 参数保护 */
    if (x0 >= ST7789_WIDTH)  x0 = ST7789_WIDTH - 1;
    if (x1 >= ST7789_WIDTH)  x1 = ST7789_WIDTH - 1;
    if (y0 >= ST7789_HEIGHT) y0 = ST7789_HEIGHT - 1;
    if (y1 >= ST7789_HEIGHT) y1 = ST7789_HEIGHT - 1;

    uint32_t count = (uint32_t)(x1 - x0 + 1) * (y1 - y0 + 1);

    ST7789_SetWindow(x0, y0, x1, y1);
    ST7789_WriteData16Burst(color, count);
}

void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;

    ST7789_SetWindow(x, y, x, y);
    ST7789_WriteData16(color);
}

void ST7789_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                     uint16_t color)
{
    /* Bresenham 画线算法 */
    int16_t dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int16_t dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;

    while (1) {
        ST7789_DrawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void ST7789_DrawRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                     uint16_t color)
{
    ST7789_DrawLine(x0, y0, x1, y0, color);  /* 上 */
    ST7789_DrawLine(x0, y1, x1, y1, color);  /* 下 */
    ST7789_DrawLine(x0, y0, x0, y1, color);  /* 左 */
    ST7789_DrawLine(x1, y0, x1, y1, color);  /* 右 */
}

void ST7789_DrawBitmap(uint16_t x, uint16_t y, const uint8_t *bmp,
                        uint16_t w, uint16_t h, uint16_t color)
{
    /* 单色位图: 每行 = w 像素, 按 8 位对齐 */
    uint16_t bytes_per_row = (w + 7) / 8;

    ST7789_SetWindow(x, y, x + w - 1, y + h - 1);
    PIN_HIGH(ST7789_DC_Port, ST7789_DC_Pin);
    PIN_LOW(ST7789_CS_Port, ST7789_CS_Pin);

    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            uint8_t byte_idx = row * bytes_per_row + col / 8;
            uint8_t bit_idx  = 7 - (col % 8);
            uint16_t c = (bmp[byte_idx] & (1 << bit_idx)) ? color : COLOR_BLACK;
            bitbang_spi_write((uint8_t)(c >> 8));
            bitbang_spi_write((uint8_t)(c & 0xFF));
        }
    }

    PIN_HIGH(ST7789_CS_Port, ST7789_CS_Pin);
}

/* ── 文本输出 ──────────────────────────────────────── */

void ST7789_SetCursor(uint16_t x, uint16_t y)
{
    s_cursor_x = x;
    s_cursor_y = y;
}

char ST7789_WriteChar(char ch, ST7789_Font_t font, uint16_t fg, uint16_t bg)
{
    if (ch < ' ' || ch > '~') return 0;

    uint16_t idx = ch - ' ';
    /* 每个字符的字节数: width * (height + 7) / 8, 但实际按列存储 */
    /* 字模格式: 每列从上到下, 每列 ceil(height/8) 字节 */
    uint16_t bytes_per_col = (font.height + 7) / 8;
    uint16_t char_offset   = idx * font.width * bytes_per_col;

    /* 设置写字符的窗口 */
    uint16_t x0 = s_cursor_x;
    uint16_t y0 = s_cursor_y;
    uint16_t x1 = s_cursor_x + font.width - 1;
    uint16_t y1 = s_cursor_y + font.height - 1;

    /* 超出屏幕边界保护 */
    if (x1 >= ST7789_WIDTH || y1 >= ST7789_HEIGHT) return 0;

    ST7789_SetWindow(x0, y0, x1, y1);

    PIN_HIGH(ST7789_DC_Port, ST7789_DC_Pin);
    PIN_LOW(ST7789_CS_Port, ST7789_CS_Pin);

    for (uint8_t col = 0; col < font.width; col++) {
        for (uint8_t row_byte = 0; row_byte < bytes_per_col; row_byte++) {
            uint8_t data = font.data[char_offset + col * bytes_per_col + row_byte];
            for (uint8_t bit = 0; bit < 8; bit++) {
                uint16_t py = row_byte * 8 + bit;
                if (py >= font.height) break;
                uint16_t c = (data & (1 << (7 - bit))) ? fg : bg;
                bitbang_spi_write((uint8_t)(c >> 8));
                bitbang_spi_write((uint8_t)(c & 0xFF));
            }
        }
    }

    PIN_HIGH(ST7789_CS_Port, ST7789_CS_Pin);

    /* 光标右移一个字符宽度 */
    s_cursor_x += font.width;

    return ch;
}

char ST7789_WriteString(const char *str, ST7789_Font_t font,
                         uint16_t fg, uint16_t bg)
{
    while (*str) {
        if (ST7789_WriteChar(*str, font, fg, bg) == 0) break;
        str++;
    }
    return *str;
}

/* ── 滚动 ──────────────────────────────────────────── */

void ST7789_SetScrollArea(uint16_t top, uint16_t scroll, uint16_t bottom)
{
    ST7789_WriteCommand(0x33);  /* VSCRDEF */
    ST7789_WriteData16(top);
    ST7789_WriteData16(scroll);
    ST7789_WriteData16(bottom);
}

void ST7789_Scroll(uint16_t pixels)
{
    ST7789_WriteCommand(0x37);  /* VSCSAD */
    ST7789_WriteData16(pixels);
}
