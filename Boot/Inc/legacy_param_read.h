/**
 * @file    legacy_param_read.h
 * @brief   旧参数页（Page 54~63，eeprom.c 追加日志格式）只读抽取器
 *
 * 方案 v3.2 §7.3：首次转换前 BL 抽取全部 10 页旧参数（152 B）构建 244 B
 * 备份块写入 Page 8。本模块只读（纯内存访问，无 Flash 操作），与 App 的
 * ReadBufferFlash 扫描语义逐条对应。
 *
 * 旧格式两代长度（param_storage.c 兼容逻辑）：
 *   Page 54：Len=2（新）→ 回退 Len=1（仅 oled_recovery）；
 *   Page 61：Len=16（新）→ 回退 Len=1（仅 meter_coeff）。
 * 回退发生时 group_desc[].len 记录实际长度、legacy_fmt_id=1，缺失字段在
 * payload 中保持 0xFFFFFFFF（App 迁移时按"字段缺失→默认值"处理）。
 *
 * 区间校验（零浮点）：float 字段以 IEEE-754 位形参与比较——全序键映射后
 * 整数比较，NaN/Inf 直接拒绝。浮点常量仅作数据（编译器编码 IEEE 位形），
 * 不产生任何浮点运算代码（D5 约束）。
 */
#ifndef LEGACY_PARAM_READ_H
#define LEGACY_PARAM_READ_H

#include "bl_common.h"

#define LEGACY_GROUP_COUNT    10u
#define LEGACY_PAYLOAD_BYTES  152u
#define LEGACY_BLOB_BYTES     244u

/* 各组在 payload 内的固定偏移（字节，按页序连续）：
 * 54:2字 55:3 56:3 57:5 58:4 59:2半字 60:1 61:16 62:1 63:2 = 37字+2半字=152B */
#define LG_OFF_54   0u
#define LG_OFF_55   8u
#define LG_OFF_56   20u
#define LG_OFF_57   32u
#define LG_OFF_58   52u
#define LG_OFF_59   68u    /* 16bit API：2 半字 */
#define LG_OFF_60   72u
#define LG_OFF_61   76u
#define LG_OFF_62   140u
#define LG_OFF_63   144u

typedef struct
{
    uint8_t page_id;   /* 54~63 */
    uint8_t len;       /* 实际使用的记录长度（字数；54/61 含旧代回退）*/
    uint8_t api16;     /* 0=32bit API，1=16bit API（仅页 59 DAC）*/
    uint8_t found;     /* 1=页内找到有效记录 */
} legacy_group_info_t;

typedef struct
{
    legacy_group_info_t group[LEGACY_GROUP_COUNT];
    uint8_t  payload[LEGACY_PAYLOAD_BYTES];  /* 0xFF 预填，仅写入已找到字段 */
    uint8_t  legacy_fmt_id;                  /* 0=全用新代长度，1=有组回退旧代 */
} legacy_data_t;

/* 抽取全部 10 页（含 54/61 双代回退）。返回 1=至少一组有效（可构建备份），
 * 0=全部为空（全新设备/已擦除，无需备份）。*/
int legacy_extract_all(legacy_data_t *out);

/* 逐字段区间校验（§7.3 强制：越界即视为无效备份内容）。
 * 缺失字段（0xFFFFFFFF）跳过该项检查。返回 1=通过，0=有字段越界。*/
int legacy_validate_ranges(const legacy_data_t *d);

/* 构建 244 B 备份块（§7.3 布局：magic/ver/fmt/n/src/group_desc/payload/
 * rsvd/crc32）。blob 缓冲由调用方提供（≥244B）。*/
void legacy_build_blob(const legacy_data_t *d, uint8_t blob[LEGACY_BLOB_BYTES]);

#endif /* LEGACY_PARAM_READ_H */
