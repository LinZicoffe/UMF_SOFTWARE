/**
 * @file    main.c
 * @brief   Bootloader 主入口 — 工程基线占位（S9 填充完整启动状态机）
 *
 * 完整启动流程（方案 v3.2 §4.3：时钟 → 喂狗 → 通信参数 → 固件头 → 备份 →
 * 模式判定 → 跳转/升级会话）在 S9 实现。本文件当前仅保证工程可编译链接，
 * 并承接启动汇编（startup_stm32f103xb.s:125）对 SystemInit 符号的引用。
 */
#include "stm32f103xb.h"
#include "bl_common.h"

/* 启动汇编在进入 __iar_program_start 前调用（EWARM/startup_stm32f103xb.s:124-128）。
 * BL 不链接 CMSIS system 文件，时钟初始化由 bl_clock_init() 完成（S4），
 * 此处保持空实现；BL 位于 0x08000000，复位后 VTOR 缺省即正确，无需在此设置。 */
void SystemInit(void)
{
}

int main(void)
{
    /* S9 将替换为完整启动状态机（模式判定 / XModem 会话 / 跳转）*/
    for (;;)
    {
    }
}
