/**
 * @file    bl_clock.c
 * @brief   BL 时钟实现（寄存器级，零 HAL）
 *
 * 频率与 App 的 SystemClock_Config（Core/Src/main.c:268-301）一致：
 * HSE 8MHz ×PLL9 = 72MHz；AHB /1；APB1 /2（USART2 = 36MHz）；APB2 /1。
 */
#include "stm32f103xb.h"
#include "bl_clock.h"

/* 有界等待（此时 DWT 尚未启用，用空循环计数兜底）：
 * HSE 启动典型 <4ms、PLL 锁定 <2ms、SYSCLK 切换 <1us。
 * HSI 8MHz 下按最保守 4 周期/循环，500,000 次 ≈ 250ms，余量 60 倍以上。*/
#define BL_CLOCK_TIMEOUT_LOOPS  500000u

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

void bl_clock_init(void)
{
    int hse_ok = 0;

    RCC->CR |= RCC_CR_HSEON;
    if (wait_flag_set(&RCC->CR, RCC_CR_HSERDY))
    {
        hse_ok = 1;
    }

    if (hse_ok)
    {
        /* 72MHz 需要 Flash 2 等待周期 + 预取使能（先提延时再升频）*/
        FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk)
                   | FLASH_ACR_LATENCY_2 | FLASH_ACR_PRFTBE;

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
        }
        /* PLL 路径失败：关 PLL，回退 HSI */
        RCC->CR &= ~RCC_CR_PLLON;
    }

    /* HSI 8MHz 降级（复位缺省即 HSI，AHB/APB1=/1），
     * 方案 §4.3：保证时钟配置失败时仍可升级 */
    RCC->CR &= ~RCC_CR_HSEON;
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk)
               | FLASH_ACR_LATENCY_0 | FLASH_ACR_PRFTBE;
    s_sysclk_hz = 8000000u;
    s_pclk1_hz  = 8000000u;
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

    /* HSI 8MHz 用 0 等待周期 */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk)
               | FLASH_ACR_LATENCY_0 | FLASH_ACR_PRFTBE;

    s_sysclk_hz = 8000000u;
    s_pclk1_hz  = 8000000u;
}
