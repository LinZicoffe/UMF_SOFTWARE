/**
 * @file    bl_proto.h
 * @brief   XModem-CRC 升级会话状态机（方案 v3.2 §6）
 *
 * 传输约定（决策 D4/D6）：
 *   - 包 CRC16 = CCITT-FALSE（init 0xFFFF），仅配套上位机可用（D4）；
 *   - 元数据包（D6）：最后一个数据包之后、EOT 之前，工具发送一个额外
 *     SOH/128B 包（包号顺延），载荷 [0..3]="UMFM"、[4..7]=img_size（u32 LE，
 *     填充后总长）、[8..11]=crc32（u32 LE，跳过 32B 头区口径）、[12..127]
 *     全 0xFF。BL 校验包 CRC16 后存为期望值并 ACK（不写 Flash）；
 *     EOT 后复算整镜像 CRC32 与期望比对。无元数据包的 EOT ⇒ 拒绝。
 *     该约定闭合方案 §6.2"从机整镜像校验"的带内期望值缺口。
 */
#ifndef BL_PROTO_H
#define BL_PROTO_H

#include "bl_common.h"

typedef enum
{
    BL_PROTO_DONE = 0,     /* 升级成功：固件头已写、全部校验通过 */
    BL_PROTO_IDLE_TIMEOUT, /* 建立窗口 15s 耗尽且未收到任何包（调用方决定回跳）*/
    BL_PROTO_ABORTED,      /* 重传耗尽/CAN/擦写失败/镜像校验失败/长度不符 */
    BL_PROTO_REJECTED      /* 静态头字段不符或无元数据包（工具不配套）*/
} bl_proto_result_t;

/* 阻塞式升级会话。前提：USART2 已按通信参数初始化、会话期 IWDG 5s 已
 * 启动；备份失败的场合（BL_BACKUP_FAILED）调用方必须已调用
 * bl_flash_set_app_window(0) 关闭 App 区写窗口——此时会话仍可进入
 * （15s 窗口发 'C' 供恢复工具接入），但对 App 区的任何擦/写会在
 * store_block 即被白名单拒绝并 CAN 中止（旧 App 保持完好，T-26）；
 * 备份重建只能靠断电重上电（backup_ensure 重试）或 SWD。
 * 会话失败后可再次调用重试（页擦位图等状态每次重置）。*/
bl_proto_result_t bl_proto_session(void);

#endif /* BL_PROTO_H */
