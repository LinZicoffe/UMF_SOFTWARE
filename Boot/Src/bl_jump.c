/**
 * @file    bl_jump.c
 * @brief   App 跳转实现（§4.4 固化模板，禁止改动顺序）
 */
#include "stm32f103xb.h"
#include "bl_jump.h"
#include "bl_info.h"
#include "bl_iwdg.h"
#include "bl_clock.h"

bl_status_t bl_jump_to_app(void)
{
    uint32_t i;

    /* 防御：跳转前再核一次向量表（§6.3 强制回跳也必须校验）*/
    if (!bl_info_vector_ok())
    {
        return BL_ERR_ADDR;
    }

    __disable_irq();

    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL  = 0u;

    for (i = 0u; i < 8u; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }

    /* ★ ICER/ICPR 覆盖不到 SysTick(15)/PendSV(14)：挂起位在 ICSR。
     * 不清则 __enable_irq() 后立刻取向量到尚未初始化的 App 处理函数。*/
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

    /* 看门狗放宽 5s 并喂狗（覆盖 App 启动期，其 MX_IWDG_Init 较晚）*/
    bl_iwdg_start_5s();
    bl_iwdg_feed();

    /* 回 HSI 8MHz（App 时钟初始化假定从复位态起配）*/
    bl_clock_deinit();

    /* 先改 VTOR 消除挂起中断窗口，再设 MSP */
    SCB->VTOR = BL_APP_BASE;
    __DSB();
    __ISB();

    __set_MSP(*(volatile uint32_t *)BL_APP_BASE);

    /* ★ PRIMASK 不随跳转清零，App 从不使能中断 ⇒ 必须在此使能 */
    __enable_irq();
    __DSB();
    __ISB();

    ((void (*)(void))(*(volatile uint32_t *)(BL_APP_BASE + 4u)))();

    return BL_OK;    /* 不会到达；到达即异常 */
}
