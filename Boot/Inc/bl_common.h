/**
 * @file    bl_common.h
 * @brief   Bootloader 公共常量与基础类型（分区地址 / 魔数 / 状态码 / 工具宏）
 *
 * 依据《UMF_Bootloader_Upgrade_Plan.md》v3.2 §3、§4.5、§5.2、附录 A/B 冻结。
 * 本文件是 BL 侧地址与常量的"唯一事实来源"；任何改动必须同步方案 §3 分区表。
 *
 * 关键实现决策（BL更新日志.md 决策表 D1~D4）：
 *   D1 img_size ≡ XModem 实际接收并写入的总字节数（末包填充到块边界）；
 *   D2 魔数统一按 u32 小端数值比较，不做字节串比较；
 *   D4 包 CRC16 为 CCITT-FALSE（init 0xFFFF），与通用 XModem 工具不兼容，
 *      必须使用配套上位机。
 */
#ifndef BL_COMMON_H
#define BL_COMMON_H

#include <stdint.h>
#include <stddef.h>

/* ===== Flash 分区（v3.2 §3：64KB = 6KB BL + 1KB 备份 + 54KB App + 3KB 参数）===== */
#define BL_FLASH_PAGE_SIZE        1024u   /* F103xB 页大小 1KB */

#define BL_REGION_BASE            0x08000000u  /* 页 0~5  Bootloader 6,144 B（G1 上限）*/
#define BL_REGION_END             0x080017FFu

#define BL_BACKUP_PAGE_BASE       0x08001800u  /* 页 6   旧参数备份页（仅 BL，备份未就绪时可写）*/
#define BL_BACKUP_PAGE_END        0x08001BFFu

#define BL_APP_BASE               0x08001C00u  /* 页 7~60 Application 55,296 B */
#define BL_APP_END                0x0800F3FFu
#define BL_APP_LIMIT              0x0800F400u  /* 开区间上界 */
#define BL_APP_REGION_SIZE        55296u

#define BL_PARAM_PAGE1_BASE       0x0800F400u  /* 页 61 参数存储（3 页轮转）*/
#define BL_PARAM_PAGE2_BASE       0x0800F800u  /* 页 62 */
#define BL_PARAM_PAGE3_BASE       0x0800FC00u  /* 页 63 */

/* 固件头：App+0x200，与向量表（236 B）同页不重叠（v3.2 §4.5）*/
#define BL_FW_HDR_OFFSET          0x0200u
#define BL_FW_HDR_ADDR            (BL_APP_BASE + BL_FW_HDR_OFFSET)
#define BL_FW_HDR_SIZE            32u
#define BL_FW_IMG_MIN_SIZE        0x220u  /* 必须保证头区 0x200~0x21F 落在镜像内 */
#define BL_FW_IMG_MAX_SIZE        BL_APP_REGION_SIZE

/* RAM 邮箱（v3.2 §5.2）：位于两工程链接区之外，暖复位保持、冷启动无定义 */
#define BL_MAILBOX_BASE           0x20004C00u
#define BL_MAILBOX_SIZE           512u
#define BL_RAM_BASE               0x20000000u
#define BL_RAM_LIMIT              0x20004C00u  /* 跳转校验：App 初始 SP 上界 */

/* ===== 状态码（模块间统一返回值）===== */
typedef enum {
    BL_OK = 0,
    BL_ERR_PARAM,      /* 入参非法 */
    BL_ERR_ADDR,       /* 地址越界/未对齐/被擦写白名单拒绝 */
    BL_ERR_TIMEOUT,    /* 等待超时（总线/标志轮询）*/
    BL_ERR_ERASE,      /* Flash 页擦除失败 */
    BL_ERR_PROGRAM,    /* Flash 半字编程失败 */
    BL_ERR_VERIFY,     /* 回读校验失败 */
    BL_ERR_CHECKSUM,   /* CRC 校验失败 */
    BL_ERR_FORMAT,     /* 格式/字段非法 */
    BL_ERR_BACKUP      /* 备份未就绪或备份校验失败 */
} bl_status_t;

/* ===== 魔数（D2：一律按 u32 小端数值比较，v3.2 附录 B）===== */
#define BL_FW_HDR_MAGIC           0x554D4648u  /* "UMFH" 固件头（内存字节序为 "HFMU"）*/
#define BL_PAGE_MAGIC             0x50504731u  /* "PPG1" 参数页头 */
#define BL_SLOT_MAGIC             0x424C4346u  /* "BLCF" BL 通信配置槽 */
#define BL_MAILBOX_MAGIC          0x424C4F54u  /* "BLOT" RAM 邮箱 */
#define BL_BACKUP_MAGIC           0x424C4B31u  /* "BLK1" 旧参数备份块 */

/* 固件头有效性标记（最后写入，v3.2 §4.5 规则 1）*/
#define BL_FW_STATE_VALID         0xA5u

/* ===== BL 版本标识 ===== */
#define BL_VERSION                1u           /* 固件头 bl_min_ver 比较基准 */
#define BL_HW_ID                  0x0103u      /* STM32F103C8 */
#define BL_PROTO_VER              1u           /* XModem 变体协议版本（上位机握手参考）*/

/* ===== uart_config 编解码（与 App param_storage.h:73-81 位域语义完全一致）=====
 *   bit[2:0] = 波特率索引；bit[4:3] = 校验；bit[5] = 停止位；bit[7:6] = 保留 0 */
#define BL_UART_BAUD_COUNT        6u
#define BL_UART_PARITY_COUNT      3u
#define BL_UART_STOP_COUNT        2u

static inline uint8_t bl_uart_cfg_baud(uint8_t cfg)   { return (uint8_t)(cfg & 0x07u); }
static inline uint8_t bl_uart_cfg_parity(uint8_t cfg) { return (uint8_t)((cfg >> 3) & 0x03u); }
static inline uint8_t bl_uart_cfg_stop(uint8_t cfg)   { return (uint8_t)((cfg >> 5) & 0x01u); }

static inline int bl_uart_cfg_valid(uint8_t cfg)
{
    return (bl_uart_cfg_baud(cfg)   < BL_UART_BAUD_COUNT)
        && (bl_uart_cfg_parity(cfg) < BL_UART_PARITY_COUNT)
        && (bl_uart_cfg_stop(cfg)   < BL_UART_STOP_COUNT)
        && ((cfg & 0xC0u) == 0u);
}

/* 波特率表（Hz）：索引语义与 App s_baud_rate_str 一致（param_storage.c:140-142）*/
extern const uint32_t bl_baud_table[BL_UART_BAUD_COUNT];
#define BL_UART_CFG_DEFAULT       0x04u        /* 115200 8N1（缺省，v3.2 §4.7 优先级 3）*/

/* ===== 杂项 ===== */
#define BL_ARRAY_LEN(a)           (sizeof(a) / sizeof((a)[0]))

#endif /* BL_COMMON_H */
