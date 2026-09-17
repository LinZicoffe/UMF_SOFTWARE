/**
 * @file    bl_info.c
 * @brief   BL 信息层实现（固件头 / 备份 / 通信槽 / 邮箱）
 */
#include "bl_info.h"
#include "bl_flash.h"
#include "bl_crc.h"
#include "legacy_param_read.h"
#include <string.h>

/* 备份路径大缓冲（S7 审查：调用链栈峰值 ~1KB 逼近 1KB 栈预算，
 * 提升为文件级 static——BL RAM 余量 ~17KB，无压力；本模块单次顺序
 * 调用，无重入）。 */
static uint8_t  s_backup_blob[LEGACY_BLOB_BYTES];
static uint16_t s_backup_halfwords[LEGACY_BLOB_BYTES / 2u];
static legacy_data_t s_backup_data;

/* ===== 内部辅助 ===== */

static uint16_t hdr_read16(uint32_t off)
{
    return bl_flash_read16(BL_FW_HDR_ADDR + off);
}

static uint32_t hdr_read32(uint32_t off)
{
    return bl_flash_read32(BL_FW_HDR_ADDR + off);
}

/* 固件头字段偏移（§4.5 表，全部偶偏移）*/
#define HDR_OFF_MAGIC     0x00u
#define HDR_OFF_HW_ID     0x04u
#define HDR_OFF_BL_MIN    0x06u
#define HDR_OFF_APP_VER   0x08u
#define HDR_OFF_BUILD_ID  0x0Cu
#define HDR_OFF_FMT_VER   0x10u
#define HDR_OFF_IMG_SIZE  0x14u
#define HDR_OFF_CRC32     0x18u
#define HDR_OFF_HDR_CRC16 0x1Cu
#define HDR_OFF_STATE     0x1Eu

/* ===== 固件头：层 1 ===== */

bl_hdr_status_t bl_info_hdr_check(uint32_t *img_size_out, uint32_t *build_id_out)
{
    uint32_t img_size;
    uint8_t  hdr_bytes[28];              /* 0x00~0x1B，供 hdr_crc16 计算 */
    uint16_t crc_calc;
    uint16_t i;

    /* 校验顺序（§4.5 层 1）：magic→hw→bl_min_ver→字段→hdr_crc16→state */
    if (hdr_read32(HDR_OFF_MAGIC) != BL_FW_HDR_MAGIC)
    {
        return BL_HDR_BAD_MAGIC;
    }
    if (hdr_read16(HDR_OFF_HW_ID) != (uint16_t)BL_HW_ID)
    {
        return BL_HDR_BAD_HW;
    }
    if (hdr_read16(HDR_OFF_BL_MIN) > (uint16_t)BL_VERSION)
    {
        return BL_HDR_BAD_BLVER;
    }
    if ((uint8_t)hdr_read16(HDR_OFF_FMT_VER) != 1u)   /* fmt_ver @0x10 取低字节 */
    {
        return BL_HDR_BAD_FMT;
    }

    img_size = hdr_read32(HDR_OFF_IMG_SIZE);
    if ((img_size < BL_FW_IMG_MIN_SIZE) || (img_size > BL_FW_IMG_MAX_SIZE) ||
        ((img_size & 1u) != 0u) ||
        (hdr_read32(HDR_OFF_CRC32) == 0xFFFFFFFFu))
    {
        return BL_HDR_BAD_SIZE;
    }

    /* hdr_crc16 覆盖 0x00~0x1B（静态字段 + img_size + crc32）*/
    for (i = 0; i < 28u; i += 2u)
    {
        uint16_t w = hdr_read16(i);
        hdr_bytes[i]      = (uint8_t)(w & 0xFFu);
        hdr_bytes[i + 1u] = (uint8_t)((w >> 8) & 0xFFu);
    }
    crc_calc = bl_crc16_ccitt_false(hdr_bytes, 28u);
    if (crc_calc != hdr_read16(HDR_OFF_HDR_CRC16))
    {
        return BL_HDR_BAD_CRC;
    }

    /* state 唯一有效性标记，最后判定（§4.5 规则 1）*/
    {
        uint16_t w = hdr_read16(HDR_OFF_STATE);       /* 半字含 state(0x1E)+rsvd1 */
        if ((uint8_t)(w & 0xFFu) != BL_FW_STATE_VALID)
        {
            return BL_HDR_NOT_ACTIVE;
        }
    }

    if (img_size_out != NULL)   { *img_size_out = img_size; }
    if (build_id_out != NULL)   { *build_id_out = hdr_read32(HDR_OFF_BUILD_ID); }
    return BL_HDR_OK;
}

/* ===== 固件头：层 2 ===== */

int bl_info_vector_ok(void)
{
    uint32_t sp = bl_flash_read32(BL_APP_BASE);
    uint32_t pc = bl_flash_read32(BL_APP_BASE + 4u);

    if ((sp & 7u) != 0u)                        { return 0; }  /* 8 字节对齐 */
    if (sp < 0x20000000u)                       { return 0; }
    if (sp > BL_RAM_LIMIT)                      { return 0; }
    if ((sp == 0u) || (sp == 0xFFFFFFFFu))      { return 0; }
    if ((pc & 1u) == 0u)                        { return 0; }  /* Thumb 位 */
    if (pc < BL_APP_BASE)                       { return 0; }
    if (pc > BL_APP_END)                        { return 0; }
    if ((pc == 0u) || (pc == 0xFFFFFFFFu))      { return 0; }
    return 1;
}

/* ===== 固件头：层 3 辅助 ===== */

uint32_t bl_info_image_crc32(uint32_t img_size)
{
    bl_crc32_t c;
    uint32_t addr;

    /* 入参界自检（层 1 已拦，此处防 S8/S9 误用；0xFFFFFFFF 为无效哨兵，
     * 与层 1 保证的合法 crc32 != 0xFFFFFFFF 永不误匹配）*/
    if ((img_size < BL_FW_IMG_MIN_SIZE) || (img_size > BL_FW_IMG_MAX_SIZE) ||
        ((img_size & 1u) != 0u))
    {
        return 0xFFFFFFFFu;
    }

    bl_crc32_start(&c);

    /* 段 1：[APP_BASE, +0x200)，含向量表 */
    for (addr = BL_APP_BASE; addr < BL_APP_BASE + 0x200u; addr += 4u)
    {
        uint32_t w = bl_flash_read32(addr);
        uint8_t  b[4];
        b[0] = (uint8_t)(w & 0xFFu);
        b[1] = (uint8_t)((w >> 8) & 0xFFu);
        b[2] = (uint8_t)((w >> 16) & 0xFFu);
        b[3] = (uint8_t)((w >> 24) & 0xFFu);
        bl_crc32_update(&c, b, 4u);
    }

    /* 段 2：跳过 32B 头区（§4.5 规则 3，三路径同一口径）*/
    for (addr = BL_APP_BASE + 0x220u; addr < BL_APP_BASE + img_size; addr += 4u)
    {
        uint32_t w = bl_flash_read32(addr);
        uint8_t  b[4];
        b[0] = (uint8_t)(w & 0xFFu);
        b[1] = (uint8_t)((w >> 8) & 0xFFu);
        b[2] = (uint8_t)((w >> 16) & 0xFFu);
        b[3] = (uint8_t)((w >> 24) & 0xFFu);
        bl_crc32_update(&c, b, 4u);
    }

    return bl_crc32_result(&c);
}

bl_status_t bl_info_hdr_write_bl_fields(uint32_t img_size, uint32_t crc32)
{
    /* hdr_crc16 覆盖 0x00~0x1B：静态字段从 Flash 现读 + 待写的 img_size/crc32 */
    uint8_t  hdr_bytes[28];
    uint16_t crc16;
    uint16_t buf[2];
    uint16_t i;
    bl_status_t st;

    for (i = 0; i < 28u; i += 2u)
    {
        uint16_t w = hdr_read16(i);
        hdr_bytes[i]      = (uint8_t)(w & 0xFFu);
        hdr_bytes[i + 1u] = (uint8_t)((w >> 8) & 0xFFu);
    }
    memcpy(&hdr_bytes[HDR_OFF_IMG_SIZE], &img_size, 4u);
    memcpy(&hdr_bytes[HDR_OFF_CRC32],    &crc32,   4u);
    crc16 = bl_crc16_ccitt_false(hdr_bytes, 28u);

    /* img_size（0x14）*/
    buf[0] = (uint16_t)(img_size & 0xFFFFu);
    buf[1] = (uint16_t)((img_size >> 16) & 0xFFFFu);
    st = bl_flash_program_halfwords(BL_FW_HDR_ADDR + HDR_OFF_IMG_SIZE, buf, 2u);
    if (st != BL_OK) { return st; }

    /* crc32（0x18）*/
    buf[0] = (uint16_t)(crc32 & 0xFFFFu);
    buf[1] = (uint16_t)((crc32 >> 16) & 0xFFFFu);
    st = bl_flash_program_halfwords(BL_FW_HDR_ADDR + HDR_OFF_CRC32, buf, 2u);
    if (st != BL_OK) { return st; }

    /* hdr_crc16（0x1C）*/
    buf[0] = crc16;
    st = bl_flash_program_halfwords(BL_FW_HDR_ADDR + HDR_OFF_HDR_CRC16, buf, 1u);
    if (st != BL_OK) { return st; }

    /* ★ state 最后写（0x1E，半字含 rsvd1=0xFF ⇒ 写 0xFFA5）*/
    buf[0] = (uint16_t)((0xFFu << 8) | BL_FW_STATE_VALID);
    st = bl_flash_program_halfwords(BL_FW_HDR_ADDR + HDR_OFF_STATE, buf, 1u);
    if (st != BL_OK) { return st; }

    /* 回读整体校验（层 1 全过且 img_size 一致）*/
    {
        uint32_t size_r = 0u, build_r = 0u;
        if (bl_info_hdr_check(&size_r, &build_r) != BL_HDR_OK)
        {
            return BL_ERR_VERIFY;
        }
        if (size_r != img_size)
        {
            return BL_ERR_VERIFY;
        }
    }
    return BL_OK;
}

/* ===== 旧参数备份（§7.3）===== */

int bl_info_backup_is_ready(void)
{
    uint8_t  blob[LEGACY_BLOB_BYTES];
    uint32_t magic;
    uint32_t crc_stored;
    bl_crc32_t c;
    uint32_t  a;

    for (a = 0u; a < LEGACY_BLOB_BYTES; a += 2u)      /* 244B 按 2B 读 */
    {
        uint16_t w = bl_flash_read16(BL_BACKUP_PAGE_BASE + a);
        memcpy(&blob[a], &w, 2u);
    }

    memcpy(&magic, &blob[0], 4u);
    if (magic != BL_BACKUP_MAGIC)      { return 0; }
    if (blob[4] != 1u)                 { return 0; }  /* ver */
    if (blob[6] != LEGACY_GROUP_COUNT) { return 0; }  /* n_groups */

    memcpy(&crc_stored, &blob[240], 4u);
    bl_crc32_start(&c);
    bl_crc32_update(&c, blob, 240u);
    return bl_crc32_result(&c) == crc_stored;
}

bl_backup_result_t bl_info_backup_ensure(void)
{
    legacy_data_t *data = &s_backup_data;
    uint8_t  *blob = s_backup_blob;
    uint16_t *halfwords = s_backup_halfwords;
    uint32_t i;
    bl_status_t st;

    /* 已有就绪备份 ⇒ 直接可用（备份一旦就绪即永不重建，§7.3 前置条件②）*/
    if (bl_info_backup_is_ready())
    {
        return BL_BACKUP_READY;
    }

    /* 前置条件①：App 固件头有效 ⇒ 页 54~60 已是 App 代码，抽取必为垃圾 */
    if (bl_info_hdr_check(NULL, NULL) == BL_HDR_OK)
    {
        return BL_BACKUP_READY;          /* 无需备份 */
    }

    if (!legacy_extract_all(data))
    {
        return BL_BACKUP_EMPTY_SRC;      /* 无可抽取记录：无数据可丢，允许擦除 */
    }

    /* 强制区间校验（§7.3：越界即无效内容，拒绝建立备份）*/
    if (!legacy_validate_ranges(data))
    {
        return BL_BACKUP_FAILED;
    }

    legacy_build_blob(data, blob);

    /* 写入 Page 8：备份窗口仅在此处开启，任何出口必关 */
    bl_flash_set_backup_window(1);
    st = bl_flash_erase_page(BL_BACKUP_PAGE_BASE);
    if (st == BL_OK)
    {
        for (i = 0u; i < LEGACY_BLOB_BYTES / 2u; i++)
        {
            memcpy(&halfwords[i], &blob[i * 2u], 2u);
        }
        st = bl_flash_program_halfwords(BL_BACKUP_PAGE_BASE, halfwords,
                                        (uint16_t)(LEGACY_BLOB_BYTES / 2u));
    }
    if (st == BL_OK)
    {
        st = bl_flash_verify_halfwords(BL_BACKUP_PAGE_BASE, halfwords,
                                       (uint16_t)(LEGACY_BLOB_BYTES / 2u));
    }
    bl_flash_set_backup_window(0);       /* 窗口关闭（含全部失败路径）*/

    if (st != BL_OK)      { return BL_BACKUP_FAILED; }
    if (!bl_info_backup_is_ready()) { return BL_BACKUP_FAILED; }
    return BL_BACKUP_CREATED;
}

/* ===== BL 通信槽（§4.7 优先级 2）===== */

/* 参数页头（§7.2）：magic@0, seq@4, state@6, fmt@7, data_crc16@8,
 * hdr_crc16@0x0A（覆盖 0x00~0x09）；BL 槽 @0x100 */
#define PG_OFF_MAGIC  0x00u
#define PG_OFF_SEQ    0x04u
#define PG_OFF_STATE  0x06u
#define PG_OFF_HDRCRC 0x0Au
#define PG_OFF_SLOT   0x100u

static int page_header_valid(uint32_t page, uint16_t *seq_out)
{
    uint8_t  bytes[10];
    uint32_t magic = bl_flash_read32(page + PG_OFF_MAGIC);
    uint32_t o;

    if (magic != BL_PAGE_MAGIC)                                 { return 0; }
    if ((uint8_t)bl_flash_read16(page + PG_OFF_STATE) != 0xA5u) { return 0; }

    for (o = 0u; o < 10u; o += 2u)
    {
        uint16_t w = bl_flash_read16(page + o);
        bytes[o]      = (uint8_t)(w & 0xFFu);
        bytes[o + 1u] = (uint8_t)((w >> 8) & 0xFFu);
    }
    if (bl_crc16_ccitt_false(bytes, 10u) != bl_flash_read16(page + PG_OFF_HDRCRC))
    {
        return 0;
    }
    *seq_out = bl_flash_read16(page + PG_OFF_SEQ);
    return 1;
}

static int slot_read(uint32_t page, uint8_t *cfg_out)
{
    uint32_t magic = bl_flash_read32(page + PG_OFF_SLOT);
    uint8_t  bytes[8];
    uint8_t  cfg;
    uint32_t o;

    if (magic != BL_SLOT_MAGIC) { return 0; }

    for (o = 0u; o < 8u; o += 2u)
    {
        uint16_t w = bl_flash_read16(page + PG_OFF_SLOT + o);
        bytes[o]      = (uint8_t)(w & 0xFFu);
        bytes[o + 1u] = (uint8_t)((w >> 8) & 0xFFu);
    }
    if (bl_crc16_ccitt_false(bytes, 8u) != bl_flash_read16(page + PG_OFF_SLOT + 8u))
    {
        return 0;
    }
    cfg = bytes[4];                      /* uart_config @ 槽内 0x104 */
    if (!bl_uart_cfg_valid(cfg))         { return 0; }
    *cfg_out = cfg;
    return 1;
}

int bl_info_slot_uart_config(uint8_t *uart_config_out)
{
    uint32_t pages[3] = { BL_PARAM_PAGE1_BASE, BL_PARAM_PAGE2_BASE,
                          BL_PARAM_PAGE3_BASE };

    if (uart_config_out == NULL) { return 0; }

    /* 取"页头有效且 seq 最大"者（u16 回绕用 int16 差值比较）；
     * 首选页槽无效则剔除后重选（单页数据损坏不阻断通信参数获取）*/
    for (;;)
    {
        int      best = -1;
        uint16_t best_seq = 0u;
        int      have_best = 0;
        uint8_t  cfg;
        int      i;

        for (i = 0; i < 3; i++)
        {
            uint16_t seq;
            if ((pages[i] != 0u) && page_header_valid(pages[i], &seq))
            {
                if (!have_best || ((int16_t)(seq - best_seq) > 0))
                {
                    best = i;
                    best_seq = seq;
                    have_best = 1;
                }
            }
        }
        if (!have_best) { return 0; }

        if (slot_read(pages[best], &cfg))
        {
            *uart_config_out = cfg;
            return 1;
        }
        pages[best] = 0u;                /* 本函数局部副本，可安全修改 */
    }
}

/* ===== RAM 邮箱（§5.2 + D3）===== */

static volatile bl_mailbox_t *mailbox(void)
{
    return (volatile bl_mailbox_t *)BL_MAILBOX_BASE;
}

/* crc32 覆盖本地快照的前 28 字节（0x00~0x1B）*/
static uint32_t mb_crc_of(const bl_mailbox_t *mb)
{
    return bl_crc32_calc((const uint8_t *)mb, 28u);
}

int bl_info_mailbox_read(bl_mailbox_t *mb)
{
    volatile bl_mailbox_t *p = mailbox();
    bl_mailbox_t snap;

    if (mb == NULL) { return 0; }
    memset(mb, 0, sizeof(*mb));

    snap.magic     = p->magic;
    snap.magic_inv = p->magic_inv;
    if ((snap.magic != BL_MAILBOX_MAGIC) ||
        (snap.magic_inv != ~BL_MAILBOX_MAGIC))
    {
        return 0;
    }

    snap.seq                     = p->seq;
    snap.cmd                     = p->cmd;
    snap.uart_config             = p->uart_config;
    snap.boot_attempt            = p->boot_attempt;
    snap.last_verified_build_id  = p->last_verified_build_id;
    snap.crc32                   = p->crc32;

    if (mb_crc_of(&snap) != snap.crc32)
    {
        return 0;
    }
    *mb = snap;
    return 1;
}

/* 已有效邮箱的字段更新：写字段 → crc32 最后（magic 不动）*/
static void mb_update(const bl_mailbox_t *mb)
{
    volatile bl_mailbox_t *p = mailbox();
    p->seq                    = mb->seq;
    p->cmd                    = mb->cmd;
    p->uart_config            = mb->uart_config;
    p->boot_attempt           = mb->boot_attempt;
    p->last_verified_build_id = mb->last_verified_build_id;
    p->crc32                  = mb->crc32;
}

void bl_info_mailbox_pre_jump(uint32_t verified_build_id, int verified_valid,
                              uint8_t uart_config)
{
    bl_mailbox_t mb;

    if (bl_info_mailbox_read(&mb))
    {
        if (verified_valid && (mb.last_verified_build_id != verified_build_id))
        {
            /* 新镜像（build_id 变化）：G3 重新起算（S9 审查 #2）——
             * 否则升级成功后新 App 首次启动崩溃即被旧计数≥3 误锁定，
             * 失去 3 次启动机会。last_verified==0（从未验证过）同样重置：
             * 重置方向安全（S9 复审遗留角落 3）*/
            mb.boot_attempt = 0u;
        }
        /* G3：跳转前 boot_attempt+1，App 健康运行后清零（§10.2）*/
        mb.seq++;
        mb.boot_attempt++;
        if (verified_valid)
        {
            mb.last_verified_build_id = verified_build_id;   /* D3 */
        }
        mb.crc32 = mb_crc_of(&mb);
        mb_update(&mb);
    }
    else
    {
        /* 冷启动/无效：初始化全新邮箱（boot_attempt=1 使看门狗复位循环
         * 也能累计 G3）；写序：字段 → crc32 → magic_inv → magic 最后 */
        volatile bl_mailbox_t *p = mailbox();
        memset(&mb, 0, sizeof(mb));
        mb.magic    = BL_MAILBOX_MAGIC;
        mb.magic_inv = ~BL_MAILBOX_MAGIC;
        mb.seq      = 1u;
        mb.cmd      = BL_MAILBOX_CMD_NONE;
        mb.uart_config = uart_config;
        mb.boot_attempt = 1u;
        if (verified_valid)
        {
            mb.last_verified_build_id = verified_build_id;
        }
        mb.crc32 = mb_crc_of(&mb);

        p->seq                    = mb.seq;
        p->cmd                    = mb.cmd;
        p->uart_config            = mb.uart_config;
        p->boot_attempt           = mb.boot_attempt;
        p->last_verified_build_id = mb.last_verified_build_id;
        p->crc32                  = mb.crc32;
        p->magic_inv              = mb.magic_inv;
        p->magic                  = mb.magic;     /* ★ 最后写 magic */
    }
}
