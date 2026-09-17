/**
 * @file    bl_crc.h
 * @brief   CRC32（ISO-HDLC）与 CRC16（CCITT-FALSE）—— 按位实现，无查表
 *
 * 方案 v3.2 §6.2 定版（附录 B）：
 *   CRC32 = CRC-32/ISO-HDLC：poly 0x04C11DB7（反射实现用 0xEDB88320）、
 *           init/xorout 0xFFFFFFFF、refin/refout true；
 *           "" → 0x00000000，"123456789" → 0xCBF43926，单字节 0x00 → 0xD202EF8D。
 *   CRC16 = CCITT-FALSE：poly 0x1021、init 0xFFFF、无反射、xorout 0；
 *           "123456789" → 0x29B1。
 *
 * D4：CCITT-FALSE 与通用 XModem 工具（init 0x0000）不兼容，仅配套上位机可用。
 * 按位实现约 120 B（CRC32）+ 数十 B（CRC16），不建表（体积约束 G1）。
 * 流式接口用于整镜像 CRC32 跳过 32 B 固件头区的两段计算（§4.5 规则 3）。
 */
#ifndef BL_CRC_H
#define BL_CRC_H

#include <stdint.h>

typedef struct
{
    uint32_t state;
} bl_crc32_t;

/* 流式 CRC32：start → update（可多次，段间保持状态）→ result */
void     bl_crc32_start(bl_crc32_t *ctx);
void     bl_crc32_update(bl_crc32_t *ctx, const uint8_t *data, uint32_t len);
uint32_t bl_crc32_result(const bl_crc32_t *ctx);

/* 一次性便利接口（等价于 start+update+result）*/
uint32_t bl_crc32_calc(const uint8_t *data, uint32_t len);

/* CRC16-CCITT-FALSE，一次性计算（包校验场景均为整包）*/
uint16_t bl_crc16_ccitt_false(const uint8_t *data, uint32_t len);

/* 上电自检：三个 CRC32 向量 + 一个 CRC16 向量全部通过返回 1，否则 0。
 * main() 启动早期调用，失败则拒绝进入升级会话（§6.2"两侧互验"的 BL 侧）。*/
int bl_crc_self_test(void);

#endif /* BL_CRC_H */
