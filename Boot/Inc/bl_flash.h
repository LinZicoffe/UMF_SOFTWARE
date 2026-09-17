/**
 * @file    bl_flash.h
 * @brief   F1 Flash 寄存器级驱动（解锁/页擦/半字编程/回读）+ 擦写白名单守卫
 *
 * 方案 v3.2 §10.3：BL 擦写白名单 = 两段——
 *   ① App 区 0x08001C00~0x0800F3FF（升级主体，任何时候可写）；
 *   ② 备份页 Page 6 0x08001800~0x08001BFF（仅"备份未就绪"期间允许，由
 *      bl_flash_set_backup_window() 控制窗口；备份就绪后窗口永久关闭）。
 *   其余地址（含 BL 自身 0x08000000~0x080017FF、参数页 61~63）一律 BL_ERR_ADDR。
 *   注意 App 侧守卫是另一套规则（App 只允许写参数区），两套阈值不可混用。
 *
 * F1 编程规则（RM0008）：半字编程要求目标位置当前为 0xFFFF，否则硬件置 PGERR。
 * 本驱动对"目标已是期望值"的半字直接跳过（支持同包重发，T-07）；
 * 目标为其它非 0xFFFF 值则拒绝并返回 BL_ERR_PROGRAM，不触发硬件错误。
 */
#ifndef BL_FLASH_H
#define BL_FLASH_H

#include "bl_common.h"

/* 备份页写窗口开关：仅在 BL 判定"需要建立备份"时打开，备份写入并校验
 * 通过后立即关闭（§7.3 前置条件 ⇒ 备份就绪即永不重写）。*/
void bl_flash_set_backup_window(int enable);

/* App 区写窗口开关（默认开）。§7.3"备份未就绪 ⇒ 拒绝一切擦除类命令"：
 * 备份建立失败时 main 关闭此窗口，协议层对 App 区的任何擦/写立即失败
 * （保持旧 App 完好，T-26）。*/
void bl_flash_set_app_window(int enable);

/* 整页擦除。addr 必须页对齐且落在白名单内，否则 BL_ERR_ADDR。*/
bl_status_t bl_flash_erase_page(uint32_t addr);

/* 半字编程：[addr, addr + 2*count) 整体落在白名单内且半字对齐。
 * 逐半字检查状态（§6.1）；值为 0xFFFF 或目标已是期望值的半字被跳过，
 * 因此 BL_OK 契约为"目标不劣于期望值"，严格逐字比对用 verify 接口。*/
bl_status_t bl_flash_program_halfwords(uint32_t addr, const uint16_t *data, uint16_t count);

/* 回读比对（编程后校验）。*/
bl_status_t bl_flash_verify_halfwords(uint32_t addr, const uint16_t *data, uint16_t count);

/* 直接读（固件头/页头/向量表解析用）。F1 常规读无需等待。*/
static inline uint16_t bl_flash_read16(uint32_t addr)
{
    return *(volatile uint16_t *)addr;
}

static inline uint32_t bl_flash_read32(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}

#endif /* BL_FLASH_H */
