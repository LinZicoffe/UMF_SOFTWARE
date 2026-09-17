/**
 * @file    bl_flash.c
 * @brief   F1 Flash 寄存器级驱动实现（零 HAL，RM0008 §3 闪存编程手册）
 *
 * 参照 OpenBLT 的 F1 flash 驱动惯例（本项目独立实现，无代码引用）：
 * 每次操作前清历史错误标志、每半字编程后检查状态、所有等待均有界。
 */
#include "stm32f103xb.h"
#include "bl_flash.h"

/* ===== 内部状态 ===== */
static int s_backup_window;      /* 备份页写窗口（默认关闭）*/

/* 有界等待计数：72MHz 下一次空循环约 4~6 周期，
 * 100_000 次 ≈ 数 ms，远大于单半字编程(≤4us)与标志去抖，足以兜底硬件异常。*/
#define BL_FLASH_WAIT_LIMIT   100000u

/* ===== 内部辅助 — static ===== */

static int range_within(uint32_t addr, uint32_t len, uint32_t base, uint32_t end_incl)
{
    /* base <= addr <= end_incl 由第一个条件保证，end_incl-addr+1 不会下溢 */
    return (addr >= base) && (len <= (end_incl - addr + 1u));
}

/* 白名单校验：返回 1 表示 [addr, addr+len) 允许擦/写 */
static int addr_allowed(uint32_t addr, uint32_t len)
{
    if (range_within(addr, len, BL_APP_BASE, BL_APP_END))
    {
        return 1;
    }
    if (s_backup_window && range_within(addr, len, BL_BACKUP_PAGE_BASE, BL_BACKUP_PAGE_END))
    {
        return 1;
    }
    return 0;
}

static void flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK)
    {
        FLASH->KEYR = 0x45670123u;
        FLASH->KEYR = 0xCDEF89ABu;
    }
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

/* 清历史错误/EOP 标志（F1 写 1 清除）*/
static void flash_clear_flags(void)
{
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
}

/* 等待 BSY 清零：超时返回 0，正常返回 1 */
static int wait_not_busy(void)
{
    uint32_t n;
    for (n = 0; n < BL_FLASH_WAIT_LIMIT; n++)
    {
        if ((FLASH->SR & FLASH_SR_BSY) == 0u)
        {
            return 1;
        }
    }
    return 0;
}

/* ===== Public API ===== */

void bl_flash_set_backup_window(int enable)
{
    s_backup_window = (enable != 0);
}

bl_status_t bl_flash_erase_page(uint32_t addr)
{
    if ((addr & (BL_FLASH_PAGE_SIZE - 1u)) != 0u)
    {
        return BL_ERR_ADDR;              /* 未页对齐 */
    }
    if (!addr_allowed(addr, BL_FLASH_PAGE_SIZE))
    {
        return BL_ERR_ADDR;              /* 白名单拒绝 */
    }

    flash_unlock();
    flash_clear_flags();

    FLASH->AR = addr;                    /* 页地址寄存器 */
    FLASH->CR |= FLASH_CR_PER;           /* 页擦除模式 */
    FLASH->CR |= FLASH_CR_STRT;          /* 启动 */

    if (!wait_not_busy())
    {
        FLASH->CR &= ~FLASH_CR_PER;
        flash_lock();
        return BL_ERR_TIMEOUT;
    }

    {
        uint32_t sr = FLASH->SR;
        bl_status_t st = BL_OK;

        FLASH->CR &= ~FLASH_CR_PER;
        flash_clear_flags();             /* 清 EOP 与错误标志 */
        flash_lock();

        if (sr & FLASH_SR_PGERR)         { st = BL_ERR_ERASE; }
        else if (sr & FLASH_SR_WRPRTERR) { st = BL_ERR_ERASE; }
        else if ((sr & FLASH_SR_EOP) == 0u) { st = BL_ERR_ERASE; } /* 无 EOP = 未完成 */

        /* 回读校验：整页首半字与末半字须为 0xFFFF（快速抽检，整页逐字由
         * 后续 program 的 per-halfword 预检查兜底）*/
        if (st == BL_OK)
        {
            if (bl_flash_read16(addr) != 0xFFFFu ||
                bl_flash_read16(addr + BL_FLASH_PAGE_SIZE - 2u) != 0xFFFFu)
            {
                st = BL_ERR_ERASE;
            }
        }
        return st;
    }
}

bl_status_t bl_flash_program_halfwords(uint32_t addr, const uint16_t *data, uint16_t count)
{
    uint16_t i;

    if (data == 0)
    {
        return BL_ERR_PARAM;
    }
    if ((addr & 1u) != 0u)
    {
        return BL_ERR_ADDR;              /* 未半字对齐 */
    }
    if (!addr_allowed(addr, (uint32_t)count * 2u))
    {
        return BL_ERR_ADDR;
    }

    flash_unlock();

    for (i = 0; i < count; i++)
    {
        uint32_t target = addr + (uint32_t)i * 2u;
        uint16_t  value = data[i];

        if (value == 0xFFFFu)
        {
            continue;                    /* 全 1 半字无需编程（擦除态天然一致）*/
        }
        if (bl_flash_read16(target) == value)
        {
            continue;                    /* 已是期望值（同包重发，T-07）*/
        }
        if (bl_flash_read16(target) != 0xFFFFu)
        {
            flash_lock();
            return BL_ERR_PROGRAM;       /* 目标非擦除态且值不同 ⇒ 硬件必 PGERR */
        }

        flash_clear_flags();
        FLASH->CR |= FLASH_CR_PG;
        *(volatile uint16_t *)target = value;

        if (!wait_not_busy())
        {
            FLASH->CR &= ~FLASH_CR_PG;
            flash_lock();
            return BL_ERR_TIMEOUT;
        }

        {
            uint32_t sr = FLASH->SR;
            FLASH->CR &= ~FLASH_CR_PG;

            if (sr & FLASH_SR_PGERR)
            {
                flash_clear_flags();
                flash_lock();
                return BL_ERR_PROGRAM;
            }
            if (sr & FLASH_SR_WRPRTERR)
            {
                flash_clear_flags();
                flash_lock();
                return BL_ERR_ADDR;      /* 写保护（本分区不应出现，防御性处理）*/
            }
            if ((sr & FLASH_SR_EOP) == 0u)
            {
                flash_lock();
                return BL_ERR_PROGRAM;
            }
            flash_clear_flags();         /* 清 EOP，为下一半字准备 */
        }
    }

    flash_lock();
    return BL_OK;
}

bl_status_t bl_flash_verify_halfwords(uint32_t addr, const uint16_t *data, uint16_t count)
{
    uint16_t i;

    if (data == 0)
    {
        return BL_ERR_PARAM;
    }
    if (!addr_allowed(addr, (uint32_t)count * 2u) && (count != 0u))
    {
        /* 回读比对只需读权限，但仍限制在白名单内，防止误用 */
        return BL_ERR_ADDR;
    }

    for (i = 0; i < count; i++)
    {
        if (bl_flash_read16(addr + (uint32_t)i * 2u) != data[i])
        {
            return BL_ERR_VERIFY;
        }
    }
    return BL_OK;
}
