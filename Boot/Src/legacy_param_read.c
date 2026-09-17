/**
 * @file    legacy_param_read.c
 * @brief   旧参数页抽取/校验/备份块构建（只读，零 Flash 写、零浮点运算）
 *
 * 扫描语义与 App 的 eeprom.c ReadBufferFlash/ReadBufferFlash_16 逐条对应：
 * 槽位 = [头标记 0][Len 数据字]，追加式；"本槽头为 0 且下一槽首字为
 * 0xFFFF.." 的第一个槽即最新有效记录。
 */
#include "legacy_param_read.h"
#include "bl_flash.h"
#include "bl_crc.h"
#include <string.h>

/* ===== 组描述表（页序）===== */
typedef struct
{
    uint8_t  page_id;
    uint8_t  len;       /* 新代长度 */
    uint8_t  len_fallback; /* 旧代长度（0=无旧代）*/
    uint8_t  api16;
    uint16_t payload_off;
} legacy_group_def_t;

static const legacy_group_def_t s_group_def[LEGACY_GROUP_COUNT] =
{
    /* page, len, fb, api16, off */
    { 54u,  2u,  1u, 0u, LG_OFF_54 },
    { 55u,  3u,  0u, 0u, LG_OFF_55 },
    { 56u,  3u,  0u, 0u, LG_OFF_56 },
    { 57u,  5u,  0u, 0u, LG_OFF_57 },
    { 58u,  4u,  0u, 0u, LG_OFF_58 },
    { 59u,  2u,  0u, 1u, LG_OFF_59 },
    { 60u,  1u,  0u, 0u, LG_OFF_60 },
    { 61u, 16u,  1u, 0u, LG_OFF_61 },
    { 62u,  1u,  0u, 0u, LG_OFF_62 },
    { 63u,  2u,  0u, 0u, LG_OFF_63 },
};

/* 旧页基址：0x0800D800 + (page_id - 54) * 1KB
 * （与 BSP/eeprom.h ADDR_FLASH_PAGE_54 同源；旧格式事实，不随新分区调整）*/
static uint32_t page_base(uint8_t page_id)
{
    return 0x0800D800u + (uint32_t)(page_id - 54u) * 1024u;
}

/* ===== 零浮点 IEEE-754 区间比较 ===== */

/* 全序键：正数(含+0)置符号位，负数取反码 —— 键的大小序 = 浮点值大小序 */
static uint32_t fp_key(uint32_t bits)
{
    return (bits & 0x80000000u) ? (~bits) : (bits | 0x80000000u);
}

/* bits 落在 [lo, hi] 闭区间（lo/hi 为编译期编码的 IEEE 位形，无浮点运算）*/
static int fp_in_range(uint32_t bits, float lo, float hi)
{
    union { float f; uint32_t u; } l, h;

    if ((bits & 0x7F800000u) == 0x7F800000u)
    {
        return 0;                         /* NaN / ±Inf 拒绝 */
    }
    if (bits == 0x80000000u)
    {
        bits = 0u;                        /* -0.0 归一化为 +0.0（键序修正）*/
    }
    l.f = lo;
    h.f = hi;
    return (fp_key(bits) >= fp_key(l.u)) && (fp_key(bits) <= fp_key(h.u));
}

static uint32_t payload_word(const legacy_data_t *d, uint16_t off)
{
    uint32_t v;
    memcpy(&v, &d->payload[off], 4u);
    return v;
}

static int field_absent(uint32_t v)
{
    return v == 0xFFFFFFFFu;
}

/* ===== 单组读取（对应 ReadBufferFlash 语义）===== */

/* 返回 1=找到最新记录并写入 payload 对应偏移 */
static int read_group_into(const legacy_group_def_t *g, legacy_data_t *out)
{
    uint32_t base = page_base(g->page_id);
    uint8_t  lens[2];
    uint8_t  n_try = 1;
    uint8_t  i;

    lens[0] = g->len;
    if (g->len_fallback != 0u)
    {
        lens[1] = g->len_fallback;
        n_try = 2u;
    }

    for (i = 0; i < n_try; i++)
    {
        uint8_t  len  = lens[i];
        uint8_t  slot_words = (uint8_t)(len + 1u);          /* 含头标记 */
        uint32_t unit = g->api16 ? 2u : 4u;                  /* 元素宽度 */
        uint32_t fill = g->api16 ? (480u / slot_words) : (240u / slot_words);
        uint32_t slot;

        for (slot = 0u; slot < fill; slot++)
        {
            uint32_t addr = base + slot * slot_words * unit;
            uint32_t header  = g->api16 ? bl_flash_read16(addr) : bl_flash_read32(addr);
            uint32_t next    = g->api16
                             ? bl_flash_read16(addr + slot_words * unit)
                             : bl_flash_read32(addr + slot_words * unit);

            if ((header == 0u) && (next == (g->api16 ? 0xFFFFu : 0xFFFFFFFFu)))
            {
                /* 本槽为最新有效记录：拷贝 Len 个数据元素到 payload */
                uint32_t off = g->payload_off;
                uint8_t  k;
                for (k = 0u; k < len; k++)
                {
                    if (g->api16)
                    {
                        uint16_t h = bl_flash_read16(addr + unit + k * unit);
                        memcpy(&out->payload[off], &h, 2u);
                        off += 2u;
                    }
                    else
                    {
                        uint32_t w = bl_flash_read32(addr + unit + k * unit);
                        memcpy(&out->payload[off], &w, 4u);
                        off += 4u;
                    }
                }
                /* 单条旧代记录（页 54：Len=1）会被 Len=2 视角同步"误读"
                 * （App 的兼容读法同样如此）——按"w1 缺失即旧代"修正标记，
                 * 使 desc.len 与事实一致（数据本身无歧义）。*/
                if ((len == 2u) && (g->len_fallback == 1u))
                {
                    uint32_t w1 = g->api16
                                ? bl_flash_read16(addr + 2u * unit)
                                : bl_flash_read32(addr + 2u * unit);
                    if (w1 == (g->api16 ? 0xFFFFu : 0xFFFFFFFFu))
                    {
                        out->group[g->page_id - 54u].len = g->len_fallback;
                        out->legacy_fmt_id = 1u;
                    }
                    else
                    {
                        out->group[g->page_id - 54u].len = len;
                    }
                }
                else
                {
                    out->group[g->page_id - 54u].len = len;
                }
                if (i != 0u)
                {
                    out->legacy_fmt_id = 1u;   /* 使用了旧代长度 */
                }
                return 1;
            }
        }
    }
    return 0;
}

int legacy_extract_all(legacy_data_t *out)
{
    uint8_t i;
    int any = 0;

    memset(out, 0xFF, sizeof(*out));
    out->legacy_fmt_id = 0u;

    for (i = 0u; i < LEGACY_GROUP_COUNT; i++)
    {
        out->group[i].page_id = s_group_def[i].page_id;
        out->group[i].len     = s_group_def[i].len;   /* 缺省新代长度 */
        out->group[i].api16   = s_group_def[i].api16;
        out->group[i].found   = 0u;                   /* memset 0xFF 后必须显式清零 */
        if (read_group_into(&s_group_def[i], out))
        {
            out->group[i].found = 1u;
            any = 1;
        }
    }
    return any;
}

/* ===== 区间校验（§7.3 强制清单；缺失字段跳过）=====
 *
 * 校准原则（S6 审查修正）：只做方案 §7.3 明确列出的强制检查——
 *   cal_k / cal_pct 单调 / modbus_addr / baud idx / sample_interval /
 *   filter_window_count / 15 个 float 的 NaN/Inf+MIN/MAX。
 * 刻意【不做】App 读取路径会自愈（clamp/清洗/默认值）的检查：
 *   DAC 零满度组合（Data_Init 自愈 12100/60000）、oled_recovery、
 *   cal_enabled、language、std_cond/flow_unit/total_unit、pulse_equiv、
 *   uart_config bit[7:6] 保留位（App 按位域掩码后接受）。
 * 理由：备份校验过严会把"App 正常自愈运行"的设备挡在升级门外
 * （备份未就绪 ⇒ 拒绝一切擦除 ⇒ 该设备永久无法升级，且 BL 无本地提示）。
 * 自愈类字段以原始值入备份，App 恢复时走与现网完全相同的自愈逻辑。*/
static int check_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    return field_absent(v) || ((v >= lo) && (v <= hi));
}

static int check_f32(uint32_t v, float lo, float hi)
{
    return field_absent(v) || fp_in_range(v, lo, hi);
}

int legacy_validate_ranges(const legacy_data_t *d)
{
    /* 页 54：[oled_recovery:16|window_count:16]、sample_interval_ms
     * （oled_recovery 不在强制清单，App clamp；仅查 window/sample）*/
    {
        uint32_t w0 = payload_word(d, LG_OFF_54);
        uint32_t w1 = payload_word(d, LG_OFF_54 + 4u);
        uint16_t win_cnt = (uint16_t)(w0 >> 16);

        if (!field_absent(w0) && (win_cnt != 0u) &&
            ((win_cnt < 2u) || (win_cnt > 10u)))
        {
            return 0;
        }
        if (!check_u32(w1, 100u, 60000u)) return 0;  /* sample_interval_ms */
    }

    /* 页 55：small_signal / filter_time / damping_time */
    if (!check_f32(payload_word(d, LG_OFF_55),      0.0f,   10.0f))    return 0;
    if (!check_f32(payload_word(d, LG_OFF_55 + 4u), 0.1f,  100.0f))    return 0;
    if (!check_f32(payload_word(d, LG_OFF_55 + 8u), 0.1f,  100.0f))    return 0;

    /* 页 56：freq_output（pulse_equiv / language 自愈类，不查）*/
    if (!check_f32(payload_word(d, LG_OFF_56),      0.0f, 10000.0f))   return 0;

    /* 页 57：density / pipe_diameter / gas_ref_press / gas_ref_temp / reynolds_k */
    if (!check_f32(payload_word(d, LG_OFF_57),      0.001f, 99999.0f)) return 0;
    if (!check_f32(payload_word(d, LG_OFF_57 + 4u), 0.1f,   99999.0f)) return 0;
    if (!check_f32(payload_word(d, LG_OFF_57 + 8u), 0.0f,   99999.0f)) return 0;
    if (!check_f32(payload_word(d, LG_OFF_57 + 12u), -40.0f, 200.0f))  return 0;
    if (!check_f32(payload_word(d, LG_OFF_57 + 16u), 0.001f, 10.0f))   return 0;

    /* 页 58：modbus_addr / uart_config / total_factor / preset_total */
    if (!check_u32(payload_word(d, LG_OFF_58),      1u,   247u))       return 0;
    {
        /* baud idx ≤ 5：按 App 读取路径语义（位域掩码后校验，bit[7:6] 不拒绝）*/
        uint32_t cfg = payload_word(d, LG_OFF_58 + 4u);
        if (!field_absent(cfg) && (bl_uart_cfg_baud((uint8_t)cfg) >= BL_UART_BAUD_COUNT))
        {
            return 0;
        }
    }
    if (!check_f32(payload_word(d, LG_OFF_58 + 8u), 0.001f, 99.999f))  return 0;
    if (!check_f32(payload_word(d, LG_OFF_58 + 12u), 0.0f, 9999999.0f)) return 0;

    /* 页 59（16bit API）：DacZero / DacFull —— 全部自愈类（App Data_Init
     * 对 zero<100 / full==0 / zero>=full 均自愈并正常运行），不查 */

    /* 页 60：std_cond / flow_unit / total_unit —— 自愈类（App clamp），不查 */

    /* 页 61：meter_coeff / cal_k[7] / cal_pct[7]（cal_enabled 自愈类不查）*/
    if (!check_f32(payload_word(d, LG_OFF_61), 0.001f, 99.999f)) return 0;
    {
        uint8_t  k;
        uint32_t prev_key = 0u;
        int      have_prev = 0;
        for (k = 0u; k < 7u; k++)
        {
            uint32_t kk = payload_word(d, LG_OFF_61 + 8u + (uint32_t)k * 4u);
            uint32_t pp = payload_word(d, LG_OFF_61 + 36u + (uint32_t)k * 4u);
            if (!check_f32(kk, 0.5f, 2.0f)) return 0;
            if (!check_f32(pp, 0.0f, 100.0f)) return 0;
            if (!field_absent(pp))
            {
                uint32_t key;
                if (pp == 0x80000000u) { pp = 0u; }  /* -0.0 归一化（与 fp_in_range 一致）*/
                key = fp_key(pp);
                if (have_prev && (key < prev_key)) return 0;   /* 单调不减 */
                prev_key  = key;
                have_prev = 1;
            }
        }
    }

    /* 页 62：medium_coeff */
    if (!check_f32(payload_word(d, LG_OFF_62), 0.1f, 10.0f)) return 0;

    /* 页 63：value_4ma / value_20ma */
    if (!check_f32(payload_word(d, LG_OFF_63),      -9999.0f, 99999.0f)) return 0;
    if (!check_f32(payload_word(d, LG_OFF_63 + 4u),   0.1f,   99999.0f)) return 0;

    return 1;
}

/* ===== 备份块构建（§7.3 布局，244 B）===== */
void legacy_build_blob(const legacy_data_t *d, uint8_t blob[LEGACY_BLOB_BYTES])
{
    uint8_t i;

    memset(blob, 0xFF, LEGACY_BLOB_BYTES);

    blob[0] = (uint8_t)(BL_BACKUP_MAGIC & 0xFFu);         /* 小端落盘 */
    blob[1] = (uint8_t)((BL_BACKUP_MAGIC >> 8) & 0xFFu);
    blob[2] = (uint8_t)((BL_BACKUP_MAGIC >> 16) & 0xFFu);
    blob[3] = (uint8_t)((BL_BACKUP_MAGIC >> 24) & 0xFFu);
    blob[4] = 1u;                                         /* ver */
    blob[5] = d->legacy_fmt_id;
    blob[6] = LEGACY_GROUP_COUNT;
    blob[7] = 0u;                                         /* src_epoch: 0=旧10页抽取 */

    for (i = 0u; i < LEGACY_GROUP_COUNT; i++)
    {
        uint8_t *desc = &blob[8u + (uint32_t)i * 4u];
        desc[0] = d->group[i].page_id;
        desc[1] = d->group[i].found ? d->group[i].len : 0u;
        desc[2] = d->group[i].api16;
        desc[3] = 0xFFu;                                   /* rsvd */
    }

    memcpy(&blob[48], d->payload, LEGACY_PAYLOAD_BYTES);   /* 152 B @48 */

    /* rsvd 40B @200 保持 0xFF；crc32 @240 覆盖 0~239 */
    {
        bl_crc32_t c;
        uint32_t crc;
        bl_crc32_start(&c);
        bl_crc32_update(&c, blob, 240u);
        crc = bl_crc32_result(&c);
        blob[240] = (uint8_t)(crc & 0xFFu);
        blob[241] = (uint8_t)((crc >> 8) & 0xFFu);
        blob[242] = (uint8_t)((crc >> 16) & 0xFFu);
        blob[243] = (uint8_t)((crc >> 24) & 0xFFu);
    }
}
