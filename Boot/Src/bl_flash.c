/**
 * @file    bl_flash.c
 * @brief   F1 Flash 寄存器级驱动实现（零 HAL，RM0008 §3 闪存编程手册）
 *
 * 参照 OpenBLT 的 F1 flash 驱动惯例（本项目独立实现，无代码引用）：
 * 每次操作前清历史错误标志并确认 BSY 已清零、每半字编程后检查状态、
 * 所有等待均有界且超时上限按操作类型分档。
 */
#include "stm32f103xb.h"
#include "bl_flash.h"

/* ===== 内部状态 ===== */
static int s_backup_window;      /* 备份页写窗口（默认关闭）*/

/* 有界等待上限（迭代次数，按 72MHz、每循环 4~15 周期估算）：
 *   半字编程 t_PROG ≤ 4us   ⇒ 100k 次 ≈ 5~20ms，余量 >1000 倍；
 *   页擦除   t_ERASE max 40ms ⇒ 40ms×72MHz/4周期 = 720k 次，
 *           取 2,000,000 次（最保守 4 周期/循环时约 111ms）≈ 2.8 倍余量。*/
#define BL_FLASH_WAIT_PROGRAM  100000u
#define BL_FLASH_WAIT_ERASE    2000000u

/* ===== 内部辅助 — static ===== */

static int range_within(uint32_t addr, uint32_t len, uint32_t base, uint32_t end_incl)
{
    /* 三段判据缺一不可：addr 在区间内（含上下界）且 [addr,addr+len) 不越过 end_incl。
     * 只有 addr<=end_incl 成立后，end_incl-addr+1 才不会无符号下溢。*/
    return (addr >= base) && (addr <= end_incl) && (len <= (end_incl - addr + 1u));
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

/* 等待 BSY 清零：超时返回 0，正常返回 1（limit 按操作类型分档）*/
static int wait_not_busy(uint32_t limit)
{
    uint32_t n;
    for (n = 0; n < limit; n++)
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
    uint32_t sr;

    if ((addr & (BL_FLASH_PAGE_SIZE - 1u)) != 0u)
    {
        return BL_ERR_ADDR;              /* 未页对齐 */
    }
    if (!addr_allowed(addr, BL_FLASH_PAGE_SIZE))
    {
        return BL_ERR_ADDR;              /* 白名单拒绝 */
    }

    flash_unlock();

    /* RM0008 擦除流程第 1 步：确认 BSY=0（前序操作不得悬挂）*/
    if (!wait_not_busy(BL_FLASH_WAIT_PROGRAM))
    {
        flash_lock();
        return BL_ERR_TIMEOUT;
    }

    flash_clear_flags();
    FLASH->CR |= FLASH_CR_PER;           /* 页擦除模式（RM0008 顺序：PER→AR→STRT）*/
    FLASH->AR  = addr;                   /* 页地址寄存器 */
    FLASH->CR |= FLASH_CR_STRT;          /* 启动 */

    if (!wait_not_busy(BL_FLASH_WAIT_ERASE))
    {
        /* BSY 置位期间写 CR 不生效（RM0008），故此处不做 CR 复位、仅尝试上锁并
         * 报错；真正的恢复路径是会话期看门狗（5s）复位整机后重入升级模式。*/
        flash_lock();
        return BL_ERR_TIMEOUT;
    }

    sr = FLASH->SR;
    FLASH->CR &= ~FLASH_CR_PER;
    flash_clear_flags();                 /* 清 EOP 与错误标志 */
    flash_lock();

    if (sr & FLASH_SR_PGERR)             { return BL_ERR_ERASE; }
    if (sr & FLASH_SR_WRPRTERR)          { return BL_ERR_ERASE; }
    if ((sr & FLASH_SR_EOP) == 0u)       { return BL_ERR_ERASE; } /* 无 EOP = 未完成 */

    /* 回读抽检：整页首/末半字须为 0xFFFF（页内逐字由编程期 per-halfword
     * 预检查兜底；此抽检用于拦截"部分擦除"页）*/
    if (bl_flash_read16(addr) != 0xFFFFu ||
        bl_flash_read16(addr + BL_FLASH_PAGE_SIZE - 2u) != 0xFFFFu)
    {
        return BL_ERR_ERASE;
    }
    return BL_OK;
}

/* 注意契约：value==0xFFFF 或目标已是期望值的半字会被跳过并计入成功，
 * 因此 BL_OK 表示"目标不劣于期望值"，不保证逐半字等于 data；
 * 需要严格比对时调用 bl_flash_verify_halfwords。*/
bl_status_t bl_flash_program_halfwords(uint32_t addr, const uint16_t *data, uint16_t count)
{
    uint16_t i;

    if (data == NULL)
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

    /* RM0008 编程流程第 1 步：确认 BSY=0 */
    if (!wait_not_busy(BL_FLASH_WAIT_PROGRAM))
    {
        flash_lock();
        return BL_ERR_TIMEOUT;
    }

    for (i = 0; i < count; i++)
    {
        uint32_t target = addr + (uint32_t)i * 2u;
        uint16_t  value = data[i];
        uint16_t  current;
        uint32_t sr;

        if (value == 0xFFFFu)
        {
            continue;                    /* 全 1 半字无需编程（擦除态天然一致）*/
        }
        current = bl_flash_read16(target);
        if (current == value)
        {
            continue;                    /* 已是期望值（同包重发，T-07）*/
        }
        if (current != 0xFFFFu)
        {
            flash_lock();
            return BL_ERR_PROGRAM;       /* 目标非擦除态且值不同 ⇒ 硬件必 PGERR */
        }

        flash_clear_flags();
        FLASH->CR |= FLASH_CR_PG;
        *(volatile uint16_t *)target = value;

        if (!wait_not_busy(BL_FLASH_WAIT_PROGRAM))
        {
            /* BSY 期间写 CR 无效：不清 PG、仅尝试上锁并报错，恢复路径同上 */
            flash_lock();
            return BL_ERR_TIMEOUT;
        }

        sr = FLASH->SR;
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
            return BL_ERR_ADDR;          /* 写保护（本分区不应出现，防御性处理）*/
        }
        if ((sr & FLASH_SR_EOP) == 0u)
        {
            flash_lock();
            return BL_ERR_PROGRAM;
        }
        flash_clear_flags();             /* 清 EOP，为下一半字准备 */
    }

    flash_lock();
    return BL_OK;
}

bl_status_t bl_flash_verify_halfwords(uint32_t addr, const uint16_t *data, uint16_t count)
{
    uint16_t i;

    if (data == NULL)
    {
        return BL_ERR_PARAM;
    }
    /* 回读比对只需读权限，但仍限制在白名单内，防止误用（count==0 放行）*/
    if ((count != 0u) && !addr_allowed(addr, (uint32_t)count * 2u))
    {
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
