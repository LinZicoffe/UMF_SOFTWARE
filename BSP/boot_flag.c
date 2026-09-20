/**
 * @file    boot_flag.c
 * @brief   App 侧 Bootloader 接口层实现（固件头常量 / RAM 邮箱 / 上界断言）
 */
#include "boot_flag.h"
#include "app_fw_version.h"
#include <string.h>
#include <stddef.h>

/* 链接器符号：栈顶（RAM 上界 0x20004BFF ⇒ 顶 ≤ 0x20004C00）*/
#if defined(__ICCARM__)
extern const uint32_t CSTACK$$Limit;
#define APP_STACK_TOP    ((uint32_t)&CSTACK$$Limit)
#pragma location = ".fw_header"
#define FW_HEADER_ATTR   __root
#elif defined(__GNUC__)
extern uint32_t _estack;
#define APP_STACK_TOP    ((uint32_t)&_estack)
#define FW_HEADER_ATTR   __attribute__((section(".fw_header"), used))
#else
#error "Unsupported compiler"
#endif

/* ===== .fw_header 32B 常量（链接器固定放置 0x08002600，并防止裁剪）===== */
FW_HEADER_ATTR const app_fw_header_t g_app_fw_header = {
    .magic      = BOOT_FW_HDR_MAGIC,
    .hw_id      = BOOT_FW_HDR_HW_ID,
    .bl_min_ver = BOOT_FW_HDR_BL_MIN_VER,
    .app_ver    = APP_FW_VERSION,
    .build_id   = APP_BUILD_ID,
    .fmt_ver    = BOOT_FW_HDR_FMT_VER,
    .rsvd0      = {0xFFu, 0xFFu, 0xFFu},
    .bl_area    = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
                   0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu},
};

/* 布局断言（C99 负数组长度技巧）：偏移与 §4.5 表逐字段一致，改布局必须重新评审 */
typedef char bl_assert_hdr_size   [(sizeof(app_fw_header_t) == 32u)   ? 1 : -1];
typedef char bl_assert_hdr_appver [(offsetof(app_fw_header_t, app_ver)  == 0x08u) ? 1 : -1];
typedef char bl_assert_hdr_build  [(offsetof(app_fw_header_t, build_id) == 0x0Cu) ? 1 : -1];
typedef char bl_assert_hdr_fmt    [(offsetof(app_fw_header_t, fmt_ver)  == 0x10u) ? 1 : -1];
typedef char bl_assert_hdr_blarea [(offsetof(app_fw_header_t, bl_area)  == 0x14u) ? 1 : -1];
typedef char bl_assert_mb_size    [(sizeof(boot_mailbox_t) == 32u)     ? 1 : -1];
typedef char bl_assert_mb_crc     [(offsetof(boot_mailbox_t, crc32)    == 0x1Cu) ? 1 : -1];

/* ===== 启动期邮箱快照 ===== */
static boot_mailbox_t s_boot_mb;
static uint8_t        s_boot_mb_valid;

/* ===== CRC-32/ISO-HDLC ===== */
uint32_t boot_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    int      b;

    while (len-- != 0u)
    {
        crc ^= *data++;
        for (b = 0; b < 8; b++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ===== 邮箱访问 ===== */
static volatile boot_mailbox_t *mailbox(void)
{
    return (volatile boot_mailbox_t *)BOOT_MAILBOX_BASE;
}

static uint32_t mb_crc_of(const boot_mailbox_t *mb)
{
    /* crc32 覆盖 0x00~0x1B（前 7 个字段，小端内存序）*/
    return boot_crc32((const uint8_t *)mb, 28u);
}

int boot_mailbox_read(boot_mailbox_t *out)
{
    volatile boot_mailbox_t *p = mailbox();
    boot_mailbox_t mb;

    if (out == NULL) { return 0; }
    memset(out, 0, sizeof(*out));

    mb.magic                  = p->magic;
    mb.magic_inv              = p->magic_inv;
    mb.seq                    = p->seq;
    mb.cmd                    = p->cmd;
    mb.uart_config            = p->uart_config;
    mb.boot_attempt           = p->boot_attempt;
    mb.last_verified_build_id = p->last_verified_build_id;
    mb.crc32                  = p->crc32;

    if (mb.magic != BOOT_MAILBOX_MAGIC)      { return 0; }
    if (mb.magic_inv != ~BOOT_MAILBOX_MAGIC) { return 0; }
    if (mb_crc_of(&mb) != mb.crc32)          { return 0; }

    *out = mb;
    return 1;
}

/* 写序（§5.2）：字段 → crc32 → magic_inv → magic 最后 */
static void mb_write(const boot_mailbox_t *mb)
{
    volatile boot_mailbox_t *p = mailbox();

    p->seq                    = mb->seq;
    p->cmd                    = mb->cmd;
    p->uart_config            = mb->uart_config;
    p->boot_attempt           = mb->boot_attempt;
    p->last_verified_build_id = mb->last_verified_build_id;
    p->crc32                  = mb->crc32;
    p->magic_inv              = mb->magic_inv;
    p->magic                  = mb->magic;
}

void boot_mailbox_clear(void)
{
    boot_mailbox_t mb;

    if (!boot_mailbox_read(&mb)) { return; }

    mb.seq++;
    mb.cmd          = BOOT_MAILBOX_CMD_NONE;
    mb.boot_attempt = 0u;               /* App 健康：G3 计数清零（§5.2）*/
    mb.crc32        = mb_crc_of(&mb);
    mb_write(&mb);
}

void boot_mailbox_request_upgrade(uint8_t uart_config)
{
    boot_mailbox_t mb;

    if (boot_mailbox_read(&mb))
    {
        mb.seq++;
        mb.cmd = BOOT_MAILBOX_CMD_UPGRADE;
        mb.uart_config = uart_config;   /* 刷新为当前生效配置（审查 A5 Minor：
                                           同会话改波特率后触发升级，BL 按新参数守候）*/
        /* boot_attempt / last_verified_build_id 保持 */
    }
    else
    {
        memset(&mb, 0, sizeof(mb));
        mb.seq          = 1u;
        mb.cmd          = BOOT_MAILBOX_CMD_UPGRADE;
        mb.uart_config  = uart_config;
        mb.boot_attempt = 0u;
    }
    mb.magic     = BOOT_MAILBOX_MAGIC;
    mb.magic_inv = ~BOOT_MAILBOX_MAGIC;
    mb.crc32     = mb_crc_of(&mb);
    mb_write(&mb);
}

/* ===== 初始化与自检 ===== */
void boot_flag_init(void)
{
    /* RAM 上界运行期断言：栈顶不得越过 BL_RAM_LIMIT（0x20004C00）。
     * 链接器符号无存储，必须取地址才能得到其数值；违反意味着
     * 链接配置被改坏 / 邮箱区被栈侵占 —— 停在这里由看门狗复位兜底。*/
    if (APP_STACK_TOP > BOOT_MAILBOX_BASE)
    {
        for (;;) { }
    }

    s_boot_mb_valid = (uint8_t)boot_mailbox_read(&s_boot_mb);
}

uint16_t boot_flag_iap_mirror(void)
{
    return (uint16_t)((s_boot_mb_valid &&
                       (s_boot_mb.cmd == BOOT_MAILBOX_CMD_UPGRADE)) ? 1u : 0u);
}
