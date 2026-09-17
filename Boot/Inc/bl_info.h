/**
 * @file    bl_info.h
 * @brief   BL 信息层：固件头分层校验 / 旧参数备份编排 / BL 通信槽 / RAM 邮箱
 *
 * 覆盖方案 v3.2 §4.5（固件头与启动期分层校验）、§4.7（通信参数优先级 1/2）、
 * §5.2 + 决策 D3（邮箱布局，0x18 复用为 last_verified_build_id）、§7.3（备份）。
 *
 * 魔数一律按 u32 小端数值比较（D2）。
 */
#ifndef BL_INFO_H
#define BL_INFO_H

#include "bl_common.h"

/* ===== 固件头（§4.5）===== */

typedef enum
{
    BL_HDR_OK = 0,
    BL_HDR_BAD_MAGIC,      /* magic 不符 */
    BL_HDR_BAD_HW,         /* hw_id 不符 */
    BL_HDR_BAD_BLVER,      /* bl_min_ver > BL_VERSION */
    BL_HDR_BAD_FMT,        /* fmt_ver 不符 */
    BL_HDR_BAD_SIZE,       /* img_size 越界/奇数 */
    BL_HDR_BAD_CRC,        /* hdr_crc16 不符（拦下半写的头）*/
    BL_HDR_NOT_ACTIVE      /* state != 0xA5（升级中断电 ⇒ 停升级模式）*/
} bl_hdr_status_t;

/* 层 1：按 §4.5 顺序校验固件头（范围→magic→hw→bl_min_ver→字段→hdr_crc16→state）。
 * 通过时可选输出 img_size / build_id。*/
bl_hdr_status_t bl_info_hdr_check(uint32_t *img_size_out, uint32_t *build_id_out);

/* 层 2：App 向量表合法性（SP/PC 范围与 Thumb 位，§4.4）*/
int bl_info_vector_ok(void);

/* 层 3 辅助：对 Flash 中的镜像按 §4.5 规则 3 计算 CRC32（跳过 32B 头区：
 * 覆盖 [APP_BASE, +0x200) ∪ [+0x220, +img_size)）。返回值即 CRC。*/
uint32_t bl_info_image_crc32(uint32_t img_size);

/* 写固件头的 BL 字段（升级完成时由协议层调用）：
 * img_size/crc32/hdr_crc16 先写，state=0xA5 最后写（§4.5 规则 1）。
 * 前提：头区 0x14~0x1F 当前为 0xFFFF（首次写入），不擦页。*/
bl_status_t bl_info_hdr_write_bl_fields(uint32_t img_size, uint32_t crc32);

/* ===== 旧参数备份（§7.3）===== */

typedef enum
{
    BL_BACKUP_READY = 0,   /* 已就绪（此前建立）或无需建立（App 头有效）*/
    BL_BACKUP_CREATED,     /* 本次新建并回读校验通过 */
    BL_BACKUP_EMPTY_SRC,   /* 旧 10 页无可抽取的有效记录（全空或不可识别）——
                              无数据可丢，可擦除 */
    BL_BACKUP_FAILED       /* 建立失败/校验失败 ⇒ 调用方必须拒绝一切擦除 */
} bl_backup_result_t;

/* 备份编排：前置条件（固件头无效 且 无就绪备份）满足才抽取建立；
 * 备份窗口（Page 8 可写）仅在本函数内部开启，出口必关。*/
bl_backup_result_t bl_info_backup_ensure(void);

/* 备份页当前是否已有有效 blob（magic+ver+n+crc32 全过）*/
int bl_info_backup_is_ready(void);

/* ===== BL 通信槽（§4.7 优先级 2）===== */

/* 从参数页 61/62/63 读通信槽：取"页头有效且 seq 最大"的页的 +0x100 槽；
 * 槽 magic + slot_crc16 + uart_config 语义校验全过返回 1 并输出配置值。*/
int bl_info_slot_uart_config(uint8_t *uart_config_out);

/* ===== RAM 邮箱（§5.2 + D3）=====
 * 布局：magic@0x00, magic_inv@0x04, seq@0x08, cmd@0x0C, uart_config@0x10,
 *       boot_attempt@0x14, last_verified_build_id@0x18(D3), crc32@0x1C
 *       （crc32 覆盖 0x00~0x1B 共 28B；读方必须校验 magic+反码+CRC32 三者）*/

#define BL_MAILBOX_CMD_NONE     0u
#define BL_MAILBOX_CMD_UPGRADE  1u

typedef struct
{
    uint32_t magic;
    uint32_t magic_inv;
    uint32_t seq;
    uint32_t cmd;
    uint32_t uart_config;             /* 低 8 位有效 */
    uint32_t boot_attempt;            /* G3 启动计数 */
    uint32_t last_verified_build_id;  /* D3：层 3 跳过依据 */
    uint32_t crc32;
} bl_mailbox_t;

/* 读并校验邮箱；无效（含冷启动随机内容）返回 0 且 out 全部清零。*/
int bl_info_mailbox_read(bl_mailbox_t *mb);

/* 跳转前调用：邮箱有效则 boot_attempt+1（并更新 last_verified_build_id），
 * 无效则初始化全新邮箱（boot_attempt=1，使看门狗复位循环也能累计 G3）。
 * 字段先写、crc32 随后、magic 最后（§5.2 写序）。uart_config 用于全新
 * 邮箱初始化（有效邮箱的该字段保持 App 写入值不动）。*/
void bl_info_mailbox_pre_jump(uint32_t verified_build_id, int verified_valid,
                              uint8_t uart_config);

#endif /* BL_INFO_H */
