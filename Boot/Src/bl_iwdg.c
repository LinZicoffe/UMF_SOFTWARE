/**
 * @file    bl_iwdg.c
 * @brief   BL 独立看门狗实现（寄存器级，RM0008 §18 IWDG）
 */
#include "stm32f103xb.h"
#include "bl_iwdg.h"

/* PVU/RVU 同步等待上限：寄存器同步按 LSI 时钟节拍（约 5 个 LSI 周期，
 * LSI 30~60kHz ⇒ <0.2ms）。本函数正常在 72MHz 下调用，按最保守
 * 8 周期/循环计 20,000 次 ≈ 2.2ms，余量 >10 倍。*/
#define BL_IWDG_SYNC_LIMIT  20000u

/* 等 PVU/RVU 同步完成（有界），返回 1=已清零，0=超时 */
static int wait_iwdg_sync(void)
{
    uint32_t n;
    for (n = 0; n < BL_IWDG_SYNC_LIMIT; n++)
    {
        if ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) == 0u)
        {
            return 1;
        }
    }
    return 0;
}

void bl_iwdg_feed(void)
{
    IWDG->KR = 0xAAAAu;
}

void bl_iwdg_start_5s(void)
{
    /* 暖复位路径下 IWDG 可能已带残余计数在跑：先喂狗消除立即复位风险 */
    bl_iwdg_feed();

    IWDG->KR = 0x5555u;                        /* 解锁 PR/RLR 写入 */

    /* 契约前提：调用方须在旧周期内保持喂狗节奏（方案 §4.6）*/
    if (!wait_iwdg_sync())
    {
        /* 同步未完成：保持旧周期继续运行（入口已喂狗），不强写 */
        return;
    }

    IWDG->PR  = 4u;                            /* 分频 64（PR=100b）*/
    IWDG->RLR = 3124u;                         /* 64×3125/40kHz ≈ 5.0s */

    /* PR/RLR 写入后需约 5 个 LSI 周期才生效，期间的 0xAAAA 会装入旧 RLR；
     * 再等一次同步后重载，保证装入的是新周期（超时则旧周期继续，靠
     * 调用方喂狗兜底，不构成失效）*/
    (void)wait_iwdg_sync();

    IWDG->KR = 0xAAAAu;                        /* 重载新周期（改后必须立即写）*/
    IWDG->KR = 0xCCCCu;                        /* 启动（已运行时无害）*/
}
