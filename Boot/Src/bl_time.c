/**
 * @file    bl_time.c
 * @brief   BL 时基实现（DWT CYCCNT）
 */
#include "stm32f103xb.h"
#include "bl_time.h"
#include "bl_clock.h"

void bl_time_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* DWT 访问使能 */
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;             /* 计数器使能 */
    DWT->CYCCNT = 0;
}

uint32_t bl_time_now(void)
{
    return DWT->CYCCNT;
}

uint32_t bl_time_elapsed_ms(uint32_t since)
{
    uint32_t cycles_per_ms = bl_clock_sysclk_hz() / 1000u;   /* 72 或 8 */
    return (bl_time_now() - since) / cycles_per_ms;
}

void bl_time_delay_ms(uint32_t ms)
{
    uint32_t start = bl_time_now();
    while (bl_time_elapsed_ms(start) < ms)
    {
        /* 忙等 */
    }
}
