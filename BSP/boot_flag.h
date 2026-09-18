/**
 * @file    boot_flag.h
 * @brief   App 侧 Bootloader 接口层：.fw_header 固件头常量 + RAM 共享邮箱
 *
 * 依据《UMF_Bootloader_Upgrade_Plan.md》v3.2 §4.5（固件头）、§5.2（邮箱）
 * 与 Boot/Inc/bl_common.h、bl_info.h 的冻结契约实现（App 侧镜像）。
 * 字段偏移与写入顺序改动均需重新评审（同 D1~D7 级别约束）。
 */
#ifndef __BOOT_FLAG_H
#define __BOOT_FLAG_H

#include <stdint.h>

/* ===== 固件头（32 B @ App+0x200，静态字段；0x14~0x1F 为 BL 写入区）===== */
#define BOOT_FW_HDR_MAGIC      0x554D4648u  /* "UMFH"，u32 小端（D2 口径）*/
#define BOOT_FW_HDR_HW_ID      0x0103u      /* STM32F103C8 */
#define BOOT_FW_HDR_BL_MIN_VER 1u           /* 最低 BL 版本 */
#define BOOT_FW_HDR_FMT_VER    1u           /* 固件头格式版本 */

typedef struct
{
    uint32_t magic;        /* 0x00 */
    uint16_t hw_id;        /* 0x04 */
    uint16_t bl_min_ver;   /* 0x06 */
    uint32_t app_ver;      /* 0x08 (major<<16|minor<<8|patch) */
    uint32_t build_id;     /* 0x0C */
    uint8_t  fmt_ver;      /* 0x10 */
    uint8_t  rsvd0[3];     /* 0x11..0x13 固定 0xFF（上位机预检要求）*/
    uint8_t  bl_area[12];  /* 0x14..0x1F BL 写入区：img_size/crc32/hdr_crc16/state，固定 0xFF */
} app_fw_header_t;

/* .fw_header 段实体（boot_flag.c 定义，链接器固定放 0x08002600）*/
extern const app_fw_header_t g_app_fw_header;

/* ===== RAM 共享邮箱（32 B @ 0x20004C00，暖复位保持）===== */
#define BOOT_MAILBOX_BASE      0x20004C00u
#define BOOT_MAILBOX_MAGIC     0x424C4F54u  /* "BLOT" */
#define BOOT_MAILBOX_CMD_NONE     0u
#define BOOT_MAILBOX_CMD_UPGRADE  1u

typedef struct
{
    uint32_t magic;                   /* 0x00 */
    uint32_t magic_inv;               /* 0x04 */
    uint32_t seq;                     /* 0x08 */
    uint32_t cmd;                     /* 0x0C */
    uint32_t uart_config;             /* 0x10 低 8 位有效 */
    uint32_t boot_attempt;            /* 0x14 G3 启动计数 */
    uint32_t last_verified_build_id;  /* 0x18 D3 */
    uint32_t crc32;                   /* 0x1C 覆盖 0x00~0x1B（28 B）*/
} boot_mailbox_t;

/* 读并校验邮箱（magic + 反码 + CRC32 三者全过才算有效）；
 * 无效（冷启动随机内容）返回 0 且 out 清零。*/
int boot_mailbox_read(boot_mailbox_t *out);

/* App 健康后清除：cmd=NONE、boot_attempt 清零（§5.2/§5.4）。
 * 仅当邮箱当前有效才改写；写序：字段 → crc32 → magic_inv → magic。*/
void boot_mailbox_clear(void);

/* 升级请求（Modbus 40127 写 0x5AA5 后、系统复位前调用）：
 * cmd=UPGRADE，保留其余有效字段，seq+1；无效邮箱则全新初始化。*/
void boot_mailbox_request_upgrade(uint8_t uart_config);

/* ===== 初始化与自检 ===== */
/* main() 最早调用：CSTACK 上界断言 + 读取并缓存启动期邮箱快照。*/
void boot_flag_init(void);

/* 40130 IAP_FLAG_MIRROR 回显来源：启动期邮箱 cmd（0/1）。*/
uint16_t boot_flag_iap_mirror(void);

/* CRC-32/ISO-HDLC（反射 0xEDB88320，init/xorout 0xFFFFFFFF；
 * "" → 0x00000000，"123456789" → 0xCBF43926，与 BL/host 同口径）。*/
uint32_t boot_crc32(const uint8_t *data, uint32_t len);

#endif /* __BOOT_FLAG_H */
