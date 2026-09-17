/**
 * @file    main.c
 * @brief   Bootloader 主入口与启动状态机（方案 v3.2 §4.3）
 *
 * 流程（含 S4 审查修正：喂狗先于时钟初始化）：
 *   喂狗 → 时钟 → DWT 时基 → LED/PA0 GPIO → CRC 自检 → 读通信参数
 *   （邮箱→BL槽→缺省，§4.7 优先级 1/2/3）→ USART2 → 固件头层1/层2
 *   → 备份（前置条件不满足则跳过）→ 模式判定（邮箱 cmd / G3 / App 不可信
 *   / PA0 恢复）→ 跳转（含层 3 整镜像 CRC）或升级会话循环。
 */
#include "stm32f103xb.h"
#include "bl_common.h"
#include "bl_clock.h"
#include "bl_time.h"
#include "bl_iwdg.h"
#include "bl_usart.h"
#include "bl_crc.h"
#include "bl_info.h"
#include "bl_proto.h"
#include "bl_jump.h"
#include "bl_flash.h"

/* 启动汇编（startup_stm32f103xb.s:125）调用；BL 不链接 CMSIS system 文件，
 * 时钟由 bl_clock_init() 配置，BL 位于 0x08000000 复位 VTOR 缺省正确。*/
void SystemInit(void)
{
}

/* ===== LED（PB5，§4.1：快闪=升级中近似为长亮、慢闪=等待、长亮=完成）===== */

#define LED_PORT      GPIOB
#define LED_PIN       (1u << 5u)

static void led_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    /* PB5 推挽输出 2MHz：CRL 位段 [23:20] = 0b0010 */
    GPIOB->CRL = (GPIOB->CRL & ~((uint32_t)0xFu << 20)) | ((uint32_t)0x2u << 20);
    GPIOB->BSRR = (LED_PIN << 16);       /* 初始熄灭（假设高=灭，实测可调）*/
}

static void led_set(int on)
{
    if (on)
    {
        GPIOB->BSRR = (LED_PIN << 16);   /* 低电平点亮（假设低有效）*/
    }
    else
    {
        GPIOB->BSRR = LED_PIN;
    }
}

/* ===== PA0 物理恢复模式（§4.7 优先级 5）===== */

#define PA0_MASK      (1u << 0u)
#define PA0_PRESSED() ((GPIOA->IDR & PA0_MASK) == 0u)   /* 上拉，低=按下 */

static void pa0_init(void)
{
    /* PA0 输入上拉：CRL 位段 [3:0] = 0b1000（输入带上/下拉），ODR=1 选上拉 */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    GPIOA->CRL = (GPIOA->CRL & ~((uint32_t)0xFu)) | 0x8u;
    GPIOA->BSRR = PA0_MASK;              /* ODR=1：上拉 */
}

/* 上电时 PA0 已按下才进入检测：按住 ≥3s → 等释放 → 10s 内再按一次确认。
 * 未按住立即返回（正常启动零延迟，T-36）。*/
static int pa0_recovery_requested(void)
{
    uint32_t t0;

    if (!PA0_PRESSED())
    {
        return 0;
    }

    t0 = bl_time_now();
    while (bl_time_elapsed_ms(t0) < 3000u)
    {
        bl_iwdg_feed();
        if (!PA0_PRESSED())
        {
            return 0;                    /* 提前松开：正常启动 */
        }
    }

    while (PA0_PRESSED())
    {
        bl_iwdg_feed();                  /* 等待释放 */
    }

    t0 = bl_time_now();                  /* 释放后 10s 确认窗口 */
    while (bl_time_elapsed_ms(t0) < 10000u)
    {
        bl_iwdg_feed();
        if (PA0_PRESSED())
        {
            return 1;                    /* 二次确认：进入恢复模式 */
        }
    }
    return 0;
}

/* ===== 启动判定辅助 ===== */

/* App 是否可信可跳转（层 1+2；层 3 由调用方按需执行）*/
static int app_layers_1_2_ok(uint32_t *img_size, uint32_t *build_id)
{
    if (bl_info_hdr_check(img_size, build_id) != BL_HDR_OK)
    {
        return 0;
    }
    return bl_info_vector_ok();
}

/* 层 3（§4.5：冷启动必做；暖复位且 build_id 未变则跳过——D3）。
 * 返回 1=通过（或合法跳过），0=整镜像 CRC 不符（App 不可信）。
 * *verified_this_boot：本靴是否实际复算并通过（合法跳过时置 0——
 * S9 审查 #1：回跳路径的 pre_jump 不得把"未复算"标成"已验证"）。*/
static int app_layer3_ok(uint32_t img_size, uint32_t build_id, int mb_valid,
                         const bl_mailbox_t *mb, int *verified_this_boot)
{
    uint32_t crc_expect;

    *verified_this_boot = 0;

    if (mb_valid && (mb->last_verified_build_id == build_id))
    {
        return 1;                        /* 暖复位且未变：跳过（D3）*/
    }

    crc_expect = bl_flash_read32(BL_FW_HDR_ADDR + 0x18u);
    if (bl_info_image_crc32(img_size) == crc_expect)
    {
        *verified_this_boot = 1;
        return 1;
    }
    return 0;
}

int main(void)
{
    bl_mailbox_t mb;
    int      mb_valid;
    uint8_t  uart_cfg = BL_UART_CFG_DEFAULT;
    uint32_t img_size = 0u;
    uint32_t build_id = 0u;
    int      app_ok;
    int      g3_lockout = 0;
    int      app_verified = 0;   /* App 当前镜像已通过层 3（本靴复算或既往合法验证）*/
    bl_backup_result_t bk;

    /* ---- 基础初始化（§4.3；喂狗先于时钟初始化——S4 审查修正）---- */
    bl_iwdg_feed();
    bl_clock_init();
    bl_time_init();
    led_init();
    pa0_init();

    /* CRC 定版向量自检（§6.2）：失败 ⇒ 停滞快闪，绝不进入升级会话
     * （S9 审查 #10：每段延时后各喂一次狗，防旧固件 100ms 残余预算复位循环）*/
    if (!bl_crc_self_test())
    {
        for (;;)
        {
            led_set(1);
            bl_time_delay_ms(100u);
            bl_iwdg_feed();
            led_set(0);
            bl_time_delay_ms(100u);
            bl_iwdg_feed();
        }
    }

    /* ---- 通信参数（§4.7 优先级 1→2→3）---- */
    mb_valid = bl_info_mailbox_read(&mb);
    if (mb_valid && bl_uart_cfg_valid((uint8_t)mb.uart_config))
    {
        uart_cfg = (uint8_t)mb.uart_config;
    }
    else if (!bl_info_slot_uart_config(&uart_cfg))
    {
        uart_cfg = BL_UART_CFG_DEFAULT;  /* 优先级 3：缺省 115200 8N1 */
    }
    (void)bl_usart_init(uart_cfg);

    /* ---- 固件头层 1/2 + 备份 ---- */
    app_ok = app_layers_1_2_ok(&img_size, &build_id);
    bk = bl_info_backup_ensure();        /* 前置条件不满足时内部跳过 */

    /* ---- 模式判定（§4.3）---- */
    {
        int enter_upgrade = 0;

        if (mb_valid && (mb.cmd == BL_MAILBOX_CMD_UPGRADE))
        {
            enter_upgrade = 1;                       /* App 主动请求 */
        }
        if (mb_valid && (mb.boot_attempt >= 3u))
        {
            enter_upgrade = 1;                       /* G3：启动即崩 */
            g3_lockout = 1;
        }
        if (!app_ok)
        {
            enter_upgrade = 1;                       /* App 不可信 */
        }
        if (bk == BL_BACKUP_FAILED)
        {
            /* 备份失败：拒绝一切擦除（§7.3/T-26）——关闭 App 区写窗口，
             * 协议层任何擦/写立即失败，旧 App 保持完好；停留等待窗口。*/
            bl_flash_set_app_window(0);
            enter_upgrade = 1;
            g3_lockout = 1;                          /* 不回跳，等待窗口慢闪 */
        }
        if (!enter_upgrade && pa0_recovery_requested())
        {
            enter_upgrade = 1;                       /* 物理恢复（§4.7 优先级 5：
                                                       固定 115200 守候——重配串口）*/
            uart_cfg = BL_UART_CFG_DEFAULT;
            (void)bl_usart_init(uart_cfg);
        }
        if (!enter_upgrade)
        {
            int verified_now = 0;
            /* 层 3 整镜像 CRC32（冷启动必做 / 暖复位按 D3 跳过）*/
            if (!app_layer3_ok(img_size, build_id, mb_valid, &mb, &verified_now))
            {
                enter_upgrade = 1;                   /* 域外损坏：App 不可信 */
            }
            else
            {
                app_verified = 1;                    /* 复算通过或既往合法验证 */
            }
        }

        if (!enter_upgrade)
        {
            /* 正常跳转（冷启动 <150ms 含层 3；暖复位 <100ms）*/
            bl_info_mailbox_pre_jump(build_id, app_verified, uart_cfg);
            (void)bl_jump_to_app();
            /* 跳转失败（向量表突变）⇒ 落入升级模式 */
        }
    }

    /* ==== 升级模式（§4.3/§6）==== */
    bl_iwdg_start_5s();
    led_set(1);                          /* 会话进行指示（长亮）*/

    for (;;)
    {
        bl_proto_result_t r = bl_proto_session();

        if (r == BL_PROTO_DONE)
        {
            /* 升级成功：读新头 → 更新邮箱（新镜像 G3 重新起算——S9 审查 #2
             * 由 pre_jump 的 build_id 变更重置实现）→ 跳转新 App。
             * verified=1：finish_session 已做整镜像 CRC + 向量表 + 静态头全检 */
            if (bl_info_hdr_check(&img_size, &build_id) == BL_HDR_OK)
            {
                bl_info_mailbox_pre_jump(build_id, 1, uart_cfg);
                (void)bl_jump_to_app();
            }
            /* 头仍无效（不应发生）：继续会话等待重传 */
        }
        else if (g3_lockout)
        {
            /* G3/备份失败锁定：停留升级模式等待恢复（不回跳，§10.2）。
             * 下一轮 bl_proto_session 的 15s 窗口继续发 'C' 供恢复工具
             * 接入；App 区写窗口已关（备份失败）或 App 本就不可信。*/
        }
        else
        {
            /* IDLE_TIMEOUT/ABORTED/REJECTED：App 仍可信 ⇒ 回跳（§4.3
             * 空闲窗口耗尽跳 App；取消/中止亦回跳）。
             * verified 用本靴实际状态（S9 审查 #1：cmd/G3/PA0 进入升级时
             * 本靴可能从未复算层 3，不得误标"已验证"）*/
            if (app_layers_1_2_ok(&img_size, &build_id))
            {
                bl_info_mailbox_pre_jump(build_id, app_verified, uart_cfg);
                (void)bl_jump_to_app();
            }
            /* App 无效：回到等待（下一轮 bl_proto_session 的 15s 窗口）*/
        }

        /* 等待间隙慢闪指示（500ms 翻转，按实际 sysclk 换算）*/
        led_set(((bl_time_now() / (bl_clock_sysclk_hz() / 2u)) & 1u) != 0u);
        {
            uint32_t t0 = bl_time_now();
            while (bl_time_elapsed_ms(t0) < 100u)
            {
                bl_iwdg_feed();
            }
        }
    }
}
