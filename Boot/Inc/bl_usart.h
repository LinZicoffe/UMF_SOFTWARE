/**
 * @file    bl_usart.h
 * @brief   BL USART2 轮询驱动 + RS-485 半双工方向控制（PA1 = USART2_DE）
 *
 * 方案 v3.2 §4.6：发送前 PA1 拉高，等 TC=1 后拉低并保持 ≥1 字符时间；
 * 接收为轮询（RXNE），等待循环内周期喂狗（约 100ms 一次）。
 * 校验位/字长语义与 App 的 bsp_usart2_apply_uart_config 完全一致：
 * 启用校验时 M=1（9 位字长：8 数据 + 1 校验），保证 XModem 字节流不变形。
 * 注：轮询接收不检查 PE 标志——校验错误字节的数据位不受影响，错误由
 * XModem 整包 CRC16 兜底（表现为 NAK 重传）。
 */
#ifndef BL_USART_H
#define BL_USART_H

#include "bl_common.h"

/* 按 uart_config（位域语义见 bl_common.h）初始化 USART2：
 * PA2=TX(复用推挽)、PA3=RX(浮空输入)、PA1=DE(推挽输出，空闲低)。
 * BRR 由 bl_clock_pclk1_hz() 与波特率表实时计算（HSI 回退后仍正确）。*/
bl_status_t bl_usart_init(uint8_t uart_config);

/* 当前波特率（Hz），供包传输超时计算（§4.6 max(1.5s, 2×传输+0.5s)）*/
uint32_t bl_usart_baud_hz(void);

/* 轮询收 1 字节：timeout_ms 内收到返回 0~255，超时返回 -1。
 * 按墙钟每约 100ms 喂一次狗（喂狗计时跨调用持久、检查先于 RXNE 返回，
 * 字节立即命中的调用路径同样喂狗）；ORE 置位时读 DR 清除。*/
int bl_usart_getc(uint32_t timeout_ms);

/* 轮询发 1 字节 / 缓冲区：DE 拉高 → TXE 发送 → 等 TC → DE 拉低，
 * 拉低后保持 1 字符时间再返回。*/
void bl_usart_putc(uint8_t byte);
void bl_usart_send(const uint8_t *data, uint16_t len);

#endif /* BL_USART_H */
