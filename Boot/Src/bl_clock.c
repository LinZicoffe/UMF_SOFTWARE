/**
 * @file    bl_clock.c
 * @brief   BL 时钟实现（寄存器级，零 HAL）
 *
 * 频率与 App 的 SystemClock_Config（Core/Src/main.c:268-301）一致：
 * HSE 8MHz ×PLL9 = 72MHz；AHB /1；APB1 /2（USART2 = 36MHz）；APB2 /1。
 */
#include "stm32f103xb.h"
#include "bl_clock.h"

/* ACR.LATENCY[2:0] 按值编码：0=0WS、1=1WS、2=2WS（RM0008 Table 3）。
 * 注意陷阱：本仓库 CMSIS 的 FLASH_ACR_LATENCY_x 是"第 x 位"掩码
 * （_2 = 0x4 = 字段值 4，非法保留值），HAL 的 FLASH_LATENCY_2 才映射
 * FLASH_ACR_LATENCY_1（=0x2）。此处按语义显式命名，避免误用。*/
#define BL_ACR_LATENCY_2WS   FLASH_ACR_LATENCY_1   /* 72MHz 需 2 等待周期 */
#define BL_ACR_LATENCY_0WS   0u                    /* 8MHz 用 0 等待周期 */

/* 有界等待（此时 DWT 尚未启用，用空循环计数兜底）：
 * 等待事件为 HSE 就绪（typ <4ms）/ PLL 锁定（<2ms）/ SYSCLK 切换确认（<1us）。
 * 空循环按复位 HSI 8MHz、最保守 8 周期/循环计：100,000 次 ≈ 100ms，
 * 对最慢事件（HSE 4ms）余量 25 倍。上限刻意压低（而非数百 ms）是为了
 * 暖复位路径下尽快走完 HSI 回退——IWDG 残余预算的兜底见 bl_iwdg 契约。*/
#define BL_CLOCK_TIMEOUT_LOOPS  100000u

static uint32_t s_sysclk_hz = 8000000u;   /* 缺省 HSI */
static uint32_t s_pclk1_hz  = 8000000u;

static int wait_flag_set(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t n;
    for (n = 0; n < BL_CLOCK_TIMEOUT_LOOPS; n++)
    {
        if ((*reg & mask) == mask)
        {
            return 1;
        }
    }
    return 0;
}

static int wait_flag_value(volatile uint32_t *reg, uint32_t mask, uint32_t value)
{
    uint32_t n;
    for (n = 0; n < BL_CLOCK_TIMEOUT_LOOPS; n++)
    {
        if ((*reg & mask) == value)
        {
            return 1;
        }
    }
    return 0;
}

/* 回退到 HSI 8MHz：清总线分频（AHB/APB1/APB2 = /1）、降 Flash 0WS、
 * 更新频率记录。关 PLL/HSE 由各调用方在进入本函数前完成。
 * SYSCLK 已确认不在 PLL 上方可调用（先切 HSI 再关 PLL，顺序不可颠倒）。*/
static void fallback_to_hsi(void)
{
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk) | BL_ACR_LATENCY_0WS
               | FLASH_ACR_PRFTBE;
    s_sysclk_hz = 8000000u;
    s_pclk1_hz  = 8000000u;
}

void bl_clock_init(void)
{
    RCC->CR |= RCC_CR_HSEON;

    if (wait_flag_set(&RCC->CR, RCC_CR_HSERDY))
    {
        /* 72MHz 需 Flash 2 等待周期 + 预取使能（先提延时再升频）*/
        FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk)
                   | BL_ACR_LATENCY_2WS | FLASH_ACR_PRFTBE;

        /* 总线分频：AHB /1（复位值），APB1 /2，APB2 /1 */
        RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2))
                  | RCC_CFGR_PPRE1_DIV2;

        /* PLL = HSE 不分频 ×9（PLLSRC 位写 1 选 HSE，本头文件无 HSE 别名）*/
        RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL))
                  | RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE_HSE | RCC_CFGR_PLLMULL9;

        RCC->CR |= RCC_CR_PLLON;
        if (wait_flag_set(&RCC->CR, RCC_CR_PLLRDY))
        {
            RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
            if (wait_flag_value(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL))
            {
                s_sysclk_hz = 72000000u;
                s_pclk1_hz  = 36000000u;
                return;                     /* 72MHz 成功 */
            }

            /* SW 切换失败：先把 SW 写回 HSI 并等 SWS 确认，再关 PLL。
             * 直接关 PLL 在"SWS 实际已切换但轮询误判超时"的极端情形会砍掉
             * SYSCLK（内核失钟挂死），此顺序同时覆盖该情形。*/
            RCC->CFGR &= ~RCC_CFGR_SW;
            (void)wait_flag_value(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI);
        }
        /* PLL 未就绪/切换失败：SYSCLK 已回 HSI，安全关 PLL */
        RCC->CR &= ~RCC_CR_PLLON;
    }

    /* HSE 失败 → HSI 8MHz 降级（方案 §4.3：保证时钟失败仍可升级）*/
    RCC->CR &= ~RCC_CR_HSEON;
    fallback_to_hsi();
}

uint32_t bl_clock_sysclk_hz(void)
{
    return s_sysclk_hz;
}

uint32_t bl_clock_pclk1_hz(void)
{
    return s_pclk1_hz;
}

void bl_clock_deinit(void)
{
    /* 切回 HSI 并等 SWS 确认，再关 PLL/HSE（顺序不可颠倒：SYSCLK 在 PLL 上时
     * 直接关 PLL 会失时钟）*/
    RCC->CFGR &= ~RCC_CFGR_SW;
    (void)wait_flag_value(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI);

    RCC->CR &= ~RCC_CR_PLLON;
    RCC->CR &= ~RCC_CR_HSEON;

    /* 清总线分频到 /1，保证 s_pclk1_hz=8MHz 与硬件一致（HSI 下 APB1÷2
     * 会得到 4MHz，曾是不一致点）*/
    fallback_to_hsi();
}
