/**
 * @file    bl_usart.c
 * @brief   BL USART2 轮询驱动实现（寄存器级，零 HAL）
 *
 * 引脚与 App 的 usart.c（MX_USART2_UART_Init，PA2/PA3）和 gpio.c（PA1=DE）一致。
 * DE 时序对照 OpenBLT RS-485 惯例：写 DR 前拉高，TC 之后才拉低——TC 表示
 * 移位寄存器已全部移出，此时总线才真正空闲。
 */
#include "stm32f103xb.h"
#include "bl_usart.h"
#include "bl_clock.h"
#include "bl_time.h"
#include "bl_iwdg.h"

/* 等待喂狗间隔（§4.6：等待字节的轮询循环内每约 100ms 一次）*/
#define BL_USART_FEED_MS   100u

static uint32_t s_baud_hz = 115200u;

/* 波特率表（与 App param_storage.c s_baud_rate_str 索引一致）*/
const uint32_t bl_baud_table[BL_UART_BAUD_COUNT] =
{
    4800u, 9600u, 19200u, 38400u, 115200u, 2400u
};

/* 1 字符时间（10 bit：1起始+8数据+1停止，不含校验/停止位扩展，量级用途）*/
static uint32_t bl_usart_char_time_us(void)
{
    /* baud=1e6 时 10bit=10us；先乘 10 防整型截断 */
    return (10u * 1000000u) / s_baud_hz;
}

bl_status_t bl_usart_init(uint8_t uart_config)
{
    uint8_t  baud_idx = bl_uart_cfg_baud(uart_config);
    uint8_t  parity   = bl_uart_cfg_parity(uart_config);
    uint8_t  stop     = bl_uart_cfg_stop(uart_config);
    uint32_t pclk1;
    uint32_t div16;
    uint32_t cr1;

    if (!bl_uart_cfg_valid(uart_config))
    {
        return BL_ERR_PARAM;
    }

    /* 外设时钟：GPIOA + AFIO（APB2）、USART2（APB1）*/
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    /* PA1 = DE：推挽输出 2MHz，空闲低（§4.6：接收期间 PA1=低）*/
    /* PA2 = TX：复用推挽 2MHz；PA3 = RX：浮空输入 */
    {
        uint32_t crl = GPIOA->CRL;
        crl &= ~((uint32_t)0xFu << 4 | (uint32_t)0xFu << 8 | (uint32_t)0xFu << 12);
        crl |= ((uint32_t)0x2u << 4);    /* PA1: MODE=10(2MHz) CNF=00(推挽输出) */
        crl |= ((uint32_t)0xAu << 8);    /* PA2: MODE=10 CNF=10(复用推挽) */
        crl |= ((uint32_t)0x4u << 12);   /* PA3: MODE=00 CNF=01(浮空输入) */
        GPIOA->CRL = crl;
        GPIOA->BSRR = (1u << (1 + 16));   /* BR1 置位 = PA1 输出低（DE=0 接收态）*/
    }

    /* 先关 USART 再配置（幂等）*/
    USART2->CR1 = 0u;
    USART2->CR2 = 0u;
    USART2->CR3 = 0u;

    /* BRR = round(PCLK1 / baud)（即 USARTDIV×16 的定点表示）*/
    pclk1 = bl_clock_pclk1_hz();
    s_baud_hz = bl_baud_table[baud_idx];
    div16 = (pclk1 + s_baud_hz / 2u) / s_baud_hz;
    if (div16 < 1u)     { div16 = 1u; }
    if (div16 > 0xFFFFu) { div16 = 0xFFFFu; }
    USART2->BRR = div16;

    /* 停止位（CR2.STOP[13:12]：00=1位，10=2位）*/
    if (stop == 1u)
    {
        USART2->CR2 |= USART_CR2_STOP_1;   /* 2 停止位 */
    }

    /* 字长/校验：与 App 一致——启用校验时 M=1（8 数据 + 1 校验）*/
    cr1 = 0u;
    if (parity != 0u)
    {
        cr1 |= USART_CR1_M;                /* 9 位字长 */
        cr1 |= USART_CR1_PCE;              /* 校验使能 */
        if (parity == 1u)
        {
            cr1 |= USART_CR1_PS;           /* PS=1 奇校验 */
        }
    }
    cr1 |= USART_CR1_TE | USART_CR1_RE;
    cr1 |= USART_CR1_UE;                   /* UE 最后置位 */
    USART2->CR1 = cr1;

    /* 读一次 DR 清洗可能残留的 RXNE/ORE */
    (void)USART2->DR;

    return BL_OK;
}

uint32_t bl_usart_baud_hz(void)
{
    return s_baud_hz;
}

int bl_usart_getc(uint32_t timeout_ms)
{
    uint32_t start    = bl_time_now();
    uint32_t last_fd  = start;

    for (;;)
    {
        uint32_t sr = USART2->SR;

        if (sr & USART_SR_RXNE)
        {
            return (int)(USART2->DR & 0xFFu);
        }
        if (sr & USART_SR_ORE)             /* 溢出：读 SR 后读 DR 清除 */
        {
            (void)USART2->DR;
        }

        if (bl_time_elapsed_ms(start) >= timeout_ms)
        {
            return -1;                     /* 超时 */
        }

        /* 等待循环内周期喂狗（§4.6）*/
        if (bl_time_elapsed_ms(last_fd) >= BL_USART_FEED_MS)
        {
            bl_iwdg_feed();
            last_fd = bl_time_now();
        }
    }
}

void bl_usart_putc(uint8_t byte)
{
    GPIOA->BSRR = (1u << 1);               /* PA1=1，DE 发送态 */

    while ((USART2->SR & USART_SR_TXE) == 0u)
    {
        /* 等发送数据寄存器空 */
    }
    USART2->DR = byte;

    while ((USART2->SR & USART_SR_TC) == 0u)
    {
        /* 等发送完成（移位寄存器移出，总线真正空闲）*/
    }
    USART2->SR &= ~USART_SR_TC;            /* 写 0 清 TC（F1）*/

    GPIOA->BSRR = (1u << (1 + 16));        /* PA1=0，DE 接收态 */

    /* 保持 DE 低 ≥1 字符时间（§4.6），等待收发器/总线建立 */
    {
        uint32_t start = bl_time_now();
        uint32_t wait_us = bl_usart_char_time_us();
        while ((bl_time_now() - start) < wait_us * (bl_clock_sysclk_hz() / 1000000u))
        {
            /* 忙等（µs 级）*/
        }
    }
}

void bl_usart_send(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        bl_usart_putc(data[i]);
    }
}
