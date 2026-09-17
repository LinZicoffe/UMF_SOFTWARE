/**
 * @file    bl_crc.c
 * @brief   CRC32（ISO-HDLC，反射按位）与 CRC16（CCITT-FALSE，按位）实现
 */
#include "bl_crc.h"

/* ===== CRC-32/ISO-HDLC（反射实现）=====
 * 反射多项式 0xEDB88320，init 0xFFFFFFFF，输出前异或 0xFFFFFFFF。
 * 逐位处理：先处理字节的低位（refin=true）。 */
void bl_crc32_start(bl_crc32_t *ctx)
{
    ctx->state = 0xFFFFFFFFu;
}

void bl_crc32_update(bl_crc32_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t crc = ctx->state;
    uint32_t i;
    uint8_t  bit;

    for (i = 0; i < len; i++)
    {
        crc ^= (uint32_t)data[i];
        for (bit = 0; bit < 8u; bit++)
        {
            if (crc & 1u)
            {
                crc = (crc >> 1) ^ 0xEDB88320u;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    ctx->state = crc;
}

uint32_t bl_crc32_result(const bl_crc32_t *ctx)
{
    return ctx->state ^ 0xFFFFFFFFu;
}

uint32_t bl_crc32_calc(const uint8_t *data, uint32_t len)
{
    bl_crc32_t ctx;
    bl_crc32_start(&ctx);
    bl_crc32_update(&ctx, data, len);
    return bl_crc32_result(&ctx);
}

/* ===== CRC-16/CCITT-FALSE（非反射按位）=====
 * 多项式 0x1021，init 0xFFFF，MSB 先行，无输出异或。 */
uint16_t bl_crc16_ccitt_false(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFu;
    uint32_t i;
    uint8_t  bit;

    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0; bit < 8u; bit++)
        {
            if (crc & 0x8000u)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* ===== 定版测试向量自检（§6.2 表 + D4）===== */
int bl_crc_self_test(void)
{
    static const char vec_text[] = "123456789"; /* 9 字节，不含结尾 NUL */
    static const uint8_t vec_zero[1] = { 0x00 };

    /* CRC32：空串 / "123456789" / 单字节 0x00 */
    if (bl_crc32_calc((const uint8_t *)0, 0u) != 0x00000000u)       return 0;
    if (bl_crc32_calc((const uint8_t *)vec_text, 9u) != 0xCBF43926u) return 0;
    if (bl_crc32_calc(vec_zero, 1u) != 0xD202EF8Du)                 return 0;

    /* 流式分段一致性：把 9 字节拆成 4+5 两段，结果必须与整段相同
     * （整镜像 CRC32 需要跳过固件头区，依赖此性质）*/
    {
        bl_crc32_t ctx;
        bl_crc32_start(&ctx);
        bl_crc32_update(&ctx, (const uint8_t *)vec_text, 4u);
        bl_crc32_update(&ctx, (const uint8_t *)vec_text + 4u, 5u);
        if (bl_crc32_result(&ctx) != 0xCBF43926u) return 0;
    }

    /* CRC16-CCITT-FALSE："123456789" */
    if (bl_crc16_ccitt_false((const uint8_t *)vec_text, 9u) != 0x29B1u) return 0;

    return 1;
}
