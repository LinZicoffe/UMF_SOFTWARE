/**
 * @file    bl_iwdg.c
 * @brief   BL 独立看门狗实现（寄存器级，RM0008 §18 IWDG）
 */
#include "stm32f103xb.h"
#include "bl_iwdg.h"

/* PVU/LVU 更新等待上限：寄存器同步按 LSI 时钟节拍，典型几个 LSI 周期
 * （LSI 30~60kHz ⇒ <0.2ms）。空循环按 8MHz HSI 保守 4 周期/循环计，
 * 20,000 次 ≈ 10ms，余量 50 倍。*/
#define BL_IWDG_SYNC_LIMIT  20000u

void bl_iwdg_feed(void)
{
    IWDG->KR = 0xAAAAu;
}

void bl_iwdg_start_5s(void)
{
    uint32_t n;

    /* 暖复位路径下 IWDG 可能已带残余计数在跑：先喂狗消除立即复位风险 */
    bl_iwdg_feed();

    IWDG->KR = 0x5555u;                        /* 解锁 PR/RLR 写入 */

    /* 等 PVU/RVU 清零后写 PR/RLR 才有效（本 CMSIS 头文件中重载标志名为 RVU）
     * 有界等待，防御寄存器同步悬挂 */
    for (n = 0; n < BL_IWDG_SYNC_LIMIT; n++)
    {
        if ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) == 0u)
        {
            break;
        }
    }
    if (n >= BL_IWDG_SYNC_LIMIT)
    {
        /* 同步未完成：保持旧周期继续运行（已在喂狗），不再强写 */
        return;
    }

    IWDG->PR  = 4u;                            /* 分频 64（PR=100b）*/
    IWDG->RLR = 3124u;                         /* 64×3125/40kHz ≈ 5.0s */

    IWDG->KR = 0xAAAAu;                        /* 重载新周期（改后必须立即写）*/
    IWDG->KR = 0xCCCCu;                        /* 启动（已运行时无害）*/
}
