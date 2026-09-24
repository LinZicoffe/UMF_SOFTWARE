/**
 * @file    bl_proto.c
 * @brief   XModem-CRC 升级会话实现（SOH/128B 与 STX/1K 双包型）
 *
 * 时序（§4.6）：建立窗口 15s 每 1s 发 'C'；单包超时 max(1.5s, 2×传输+0.5s)
 * （9600 下 1K 包约 2.64s）；连续 NAK ≤ 6 次；收到任意字节即重置包计时。
 */
#include "bl_proto.h"
#include "bl_usart.h"
#include "bl_flash.h"
#include "bl_crc.h"
#include "bl_time.h"
#include "bl_iwdg.h"
#include "bl_info.h"
#include <string.h>

/* XModem 控制字符 */
#define XM_SOH   0x01u
#define XM_STX   0x02u
#define XM_EOT   0x04u
#define XM_ACK   0x06u
#define XM_NAK   0x15u
#define XM_CAN   0x18u
#define XM_C     0x43u

#define PROTO_ESTABLISH_MS   15000u   /* 会话建立窗口（§4.6）*/
#define PROTO_C_PERIOD_MS    1000u    /* 'C' 发送周期 */
#define PROTO_NAK_LIMIT      6u       /* 连续 NAK 上限 */
#define PROTO_POLL_MS        50u      /* 建立窗口内轮询片 */
#define PROTO_CAN_WAIT_MS    1000u    /* 等 CAN 第二字节 */

/* App 区 52 页首触擦除位图（7 字节，页数由 BL_APP_REGION_SIZE 推导）*/
#define ERASE_BITMAP_BYTES   ((BL_APP_REGION_SIZE / BL_FLASH_PAGE_SIZE + 7u) / 8u)

/* 元数据包 "UMFM"（内存字节 55 4D 46 4D；按 u32 数值比较，D2/D6）*/
#define META_MAGIC   0x4D464D55u

/* 会话状态（每次入口重置）*/
static uint16_t s_pkt[512];            /* 1024B 包数据缓冲（半字对齐使用）*/
static uint8_t  s_erased[ERASE_BITMAP_BYTES];
static uint32_t s_total;               /* 已写入字节数（含填充，D1 口径）*/
static uint32_t s_meta_size;           /* 元数据包声明的 img_size */
static uint32_t s_meta_crc;            /* 元数据包声明的 crc32 */
static uint8_t  s_meta_ok;
static uint8_t  s_hdr_checked;         /* 静态头字段已从 Flash 校验 */

static uint8_t *pkt_bytes(void)
{
    return (uint8_t *)s_pkt;
}

/* 单包传输时间 → 超时 max(1.5s, 2×传输+0.5s)（§4.6 公式）
 * 按 1K 包取值（对 128B 包偏保守，方向安全）*/
static uint32_t packet_timeout_ms(void)
{
    uint32_t tx_ms = (1024u + 5u) * 10u * 1000u / bl_usart_baud_hz();
    uint32_t t = 2u * tx_ms + 500u;
    return (t < 1500u) ? 1500u : t;
}

/* 精确读 n 字节（每字节独立超时 ⇒ 收到任意字节即重置计时）*/
static int read_exact(uint8_t *dst, uint16_t n, uint32_t tmo_ms)
{
    uint16_t i;
    for (i = 0u; i < n; i++)
    {
        int c = bl_usart_getc(tmo_ms);
        if (c < 0)
        {
            return 0;
        }
        dst[i] = (uint8_t)c;
    }
    return 1;
}

static void send_can_cancel(void)
{
    bl_usart_putc(XM_CAN);
    bl_usart_putc(XM_CAN);
}

/* NAK 计数与上限：超限发 CAN×2 并返回 0（调用方终止会话）*/
static int nak_or_abort(uint8_t *nak_count)
{
    (*nak_count)++;
    if (*nak_count > PROTO_NAK_LIMIT)
    {
        send_can_cancel();
        return 0;
    }
    bl_usart_putc(XM_NAK);
    return 1;
}

/* 页首触即擦 + 半字编程 + 回读校验（offset 相对 APP_BASE）*/
static bl_status_t store_block(uint32_t offset, const uint16_t *data, uint16_t dlen)
{
    bl_status_t st;
    uint32_t first = offset / BL_FLASH_PAGE_SIZE;
    uint32_t last   = (offset + (uint32_t)dlen - 1u) / BL_FLASH_PAGE_SIZE;
    uint32_t p;

    for (p = first; p <= last; p++)
    {
        if ((s_erased[p / 8u] & (uint8_t)(1u << (p % 8u))) == 0u)
        {
            st = bl_flash_erase_page(BL_APP_BASE + p * BL_FLASH_PAGE_SIZE);
            if (st != BL_OK)
            {
                return st;
            }
            s_erased[p / 8u] |= (uint8_t)(1u << (p % 8u));
            bl_iwdg_feed();
        }
    }

    st = bl_flash_program_halfwords(BL_APP_BASE + offset, data,
                                    (uint16_t)(dlen / 2u));
    if (st != BL_OK)
    {
        return st;
    }
    bl_iwdg_feed();
    return bl_flash_verify_halfwords(BL_APP_BASE + offset, data,
                                     (uint16_t)(dlen / 2u));
}

/* 静态头字段（App 源码编入部分）从 Flash 校验：magic/hw_id/bl_min_ver/fmt */
static int check_static_hdr(void)
{
    uint32_t base = BL_APP_BASE + 0x200u;
    if (bl_flash_read32(base) != BL_FW_HDR_MAGIC)          { return 0; }
    if (bl_flash_read16(base + 4u) != (uint16_t)BL_HW_ID)  { return 0; }
    if (bl_flash_read16(base + 6u) > (uint16_t)BL_VERSION) { return 0; }
    if ((uint8_t)bl_flash_read16(base + 0x10u) != 1u)      { return 0; }
    return 1;
}

/* 元数据包判定（D6）：128B、"UMFM"魔数、[12..127] 全 0xFF */
static int is_metadata_packet(const uint8_t *d, uint16_t dlen)
{
    uint32_t magic;
    uint16_t i;
    if (dlen != 128u) { return 0; }
    memcpy(&magic, d, 4u);
    if (magic != META_MAGIC) { return 0; }
    for (i = 12u; i < 128u; i++)
    {
        if (d[i] != 0xFFu) { return 0; }
    }
    return 1;
}

/* EOT 收尾：全量校验 + 写头（§6.1/§6.3）*/
static bl_proto_result_t finish_session(void)
{
    uint32_t crc;

    if (s_total < BL_FW_IMG_MIN_SIZE)
    {
        return BL_PROTO_ABORTED;                     /* 镜像过小 */
    }
    if (!s_meta_ok)
    {
        return BL_PROTO_REJECTED;                    /* 工具不配套（D4/D6）*/
    }
    if (s_meta_size != s_total)
    {
        return BL_PROTO_ABORTED;                     /* 元数据长度不符 */
    }
    if (!s_hdr_checked && !check_static_hdr())
    {
        return BL_PROTO_REJECTED;                    /* 静态头字段不符 */
    }
    if (!bl_info_vector_ok())
    {
        return BL_PROTO_ABORTED;                     /* 向量表非法 */
    }

    crc = bl_info_image_crc32(s_total);
    if (crc != s_meta_crc)
    {
        bl_usart_putc(XM_NAK);                       /* 整镜像 CRC 不符（§6.3）*/
        return BL_PROTO_ABORTED;
    }

    if (bl_info_hdr_write_bl_fields(s_total, crc) != BL_OK)
    {
        return BL_PROTO_ABORTED;                     /* 写头失败（state 未写）*/
    }
    return BL_PROTO_DONE;
}

/* 处理一个已通过包校验的包；*result 仅在返回 0（终止会话）时有效 */
static int handle_packet(uint16_t dlen, bl_proto_result_t *result)
{
    uint8_t *d = pkt_bytes();

    *result = BL_PROTO_DONE;

    /* 元数据包（D6）：ACK 但不写 Flash，存期望值 */
    if (is_metadata_packet(d, dlen))
    {
        memcpy(&s_meta_size, &d[4], 4u);
        memcpy(&s_meta_crc,  &d[8], 4u);
        s_meta_ok = 1u;
        bl_usart_putc(XM_ACK);
        return 1;                                    /* 继续接收（等 EOT）*/
    }

    /* 越界检查（§6.3 长度上界）*/
    if (s_total + (uint32_t)dlen > BL_APP_REGION_SIZE)
    {
        send_can_cancel();
        *result = BL_PROTO_ABORTED;
        return 0;
    }

    if (store_block(s_total, s_pkt, dlen) != BL_OK)
    {
        send_can_cancel();                           /* 擦/写/校验失败（§6.3）*/
        *result = BL_PROTO_ABORTED;
        return 0;
    }
    s_total += dlen;

    /* 静态头字段：头区（+0x200~0x21F）落入已写区域后尽早校验（省一次
     * 必败传输；§6.3 hw_id/bl_min_ver 不符 → 拒绝）*/
    if (!s_hdr_checked && (s_total > 0x220u))
    {
        if (!check_static_hdr())
        {
            send_can_cancel();
            *result = BL_PROTO_REJECTED;
            return 0;
        }
        s_hdr_checked = 1u;
    }

    bl_usart_putc(XM_ACK);
    return 1;                                        /* 继续接收 */
}

bl_proto_result_t bl_proto_session(void)
{
    uint32_t win_start;                             /* 会话建立窗口起点 */
    uint32_t last_c;                                /* 上次发 'C' 的时间戳 */
    uint32_t pkt_tmo = packet_timeout_ms();         /* 单包超时（§4.6）*/
    uint8_t  expected_blk = 1u;                     /* 下一个期望包号（XModem 1~255 回绕）*/
    uint8_t  nak_count = 0u;                        /* 连续 NAK 计数（§4.6）*/
    int      first_byte = -1;                       /* 进入接收循环前已读的首字节（SOH/STX）*/
    uint32_t i;                                     /* 用于擦除位图清零循环 */

    s_total = 0u;                                   /* 已写入字节数（含填充，D1 口径）*/
    s_meta_ok = 0u;                                 /* 元数据包已接收且校验通过（D6）*/
    s_hdr_checked = 0u;                             /* 静态头字段已校验 */
    s_meta_size = 0u;                               /* 元数据包声明的 img_size */
    s_meta_crc = 0u;                                /* 元数据包声明的 crc32 */
    // 清零擦除位图
    for (i = 0u; i < ERASE_BITMAP_BYTES; i++)
    {
        s_erased[i] = 0u;
    }

    /* ---- 会话建立窗口：15s 内每 1s 发 'C'（§4.3/§4.6）---- */
    win_start = bl_time_now();
    last_c = win_start;
    for (;;)
    {
        int c;
        // 窗口耗尽：15s 内未收到任何包（调用方决定回跳）
        if (bl_time_elapsed_ms(win_start) >= PROTO_ESTABLISH_MS)
        {
            return BL_PROTO_IDLE_TIMEOUT;
        }
        // 每 1s 发 'C'（§4.6）提醒工具发包
        if (bl_time_elapsed_ms(last_c) >= PROTO_C_PERIOD_MS)
        {
            bl_usart_putc(XM_C);
            last_c = bl_time_now();
        }
        c = bl_usart_getc(PROTO_POLL_MS);
        if (c < 0)
        {
            continue;                                /* 窗口内继续等 */
        }
        if ((c == XM_SOH) || (c == XM_STX))
        {
            first_byte = c;
            break;                                   /* 进入接收循环 */
        }
        /* 其它字节：总线噪声，忽略（§4.6 误判只导致重传，不写错数据）*/
    }

    /* ---- 接收循环 ---- */
    for (;;)
    {
        uint8_t  hdr2[2];                           /* 包号 + 反码 */
        uint8_t  crc_b[2];                          /* CRC16 高低字节 */
        uint16_t crc_recv;                          /* 接收的 CRC16 */
        uint16_t dlen;                              /* 数据长度（128B 或 1024B）*/
        int      c = first_byte;                    /* 进入循环前已读的首字节（SOH/STX）*/
        first_byte = -1;                            /* 仅在循环首轮使用，后续循环每次都从 USART 读新字节 */
        // 期望包号（XModem 1~255 回绕）与连续 NAK 计数（§4.6）在每轮循环末尾更新
        if (c < 0)
        {
            // 进入循环前未读到首字节（SOH/STX），则阻塞等待一个字节（每字节独立超时）
            c = bl_usart_getc(pkt_tmo);
            if (c < 0)
            {
                if (!nak_or_abort(&nak_count))       /* 单包超时（§4.6）*/
                {
                    return BL_PROTO_ABORTED;
                }
                continue;
            }
        }

        if (c == XM_EOT)
        {
            bl_usart_putc(XM_ACK);
            return finish_session();
        }
        if (c == XM_CAN)
        {
            c = bl_usart_getc(PROTO_CAN_WAIT_MS);
            if (c == XM_CAN)
            {
                return BL_PROTO_ABORTED;             /* CAN×2 显式中止（§8 取消）*/
            }
            if ((c == XM_SOH) || (c == XM_STX))
            {
                first_byte = c;                      /* 回灌：新包首字节不丢弃 */
            }
            continue;                                /* 单 CAN 视为噪声 */
        }
        if (c == XM_SOH)      { dlen = 128u; }
        else if (c == XM_STX) { dlen = 1024u; }
        else                  { continue; }           /* 噪声字节，重新等包头 */

        /* 包头后续：包号 + 反码 + 数据 + CRC16（每字节独立超时）*/
        if (!read_exact(hdr2, 2u, pkt_tmo) ||
            !read_exact(pkt_bytes(), dlen, pkt_tmo) ||
            !read_exact(crc_b, 2u, pkt_tmo))
        {
            if (!nak_or_abort(&nak_count))
            {
                return BL_PROTO_ABORTED;
            }
            continue;
        }

        crc_recv = (uint16_t)(((uint16_t)crc_b[0] << 8) | crc_b[1]);
        if ((hdr2[0] != expected_blk) ||
            ((uint8_t)(~hdr2[0]) != hdr2[1]) ||
            (bl_crc16_ccitt_false(pkt_bytes(), dlen) != crc_recv))
        {
            /* 重复包恢复（标准 XModem，S8 审查 #3）：ACK 丢失后工具重发
             * "前一包号"且 CRC 通过 ⇒ 该包已成功处理过，ACK 丢弃解死锁
             * （不写盘；bl_flash 等值跳过仅作兜底）。expected_blk==1 时
             * expected-1=0，包号 0 不会出现，自然不误判。*/
            if ((hdr2[0] == (uint8_t)(expected_blk - 1u)) &&
                ((uint8_t)(~hdr2[0]) == hdr2[1]) &&
                (bl_crc16_ccitt_false(pkt_bytes(), dlen) == crc_recv))
            {
                bl_usart_putc(XM_ACK);
                continue;
            }
            if (!nak_or_abort(&nak_count))
            {
                return BL_PROTO_ABORTED;
            }
            continue;
        }

        /* 包校验通过 */
        {
            bl_proto_result_t r;
            if (!handle_packet(dlen, &r))
            {
                return r;
            }
        }
        nak_count = 0u;
        expected_blk++;                              /* u8 回绕 255→0→1（XModem 惯例）*/
    }
}
