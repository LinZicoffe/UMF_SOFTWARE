/**
 * @file    param_storage.c
 * @brief   参数存储实现 — RAM 缓存 + Flash 3 页轮转（§7.2 每页完整镜像）
 *
 * A3 重写：原 Page 54~63 分散追加日志存储废弃（旧区已被 BL 分区征用），
 * 改为 Page 61~63（0x0800F400~0x0800FFFF）3 页轮转 + 每次提交整页镜像。
 * 页格式/BL 通信槽与 Boot/Inc/bl_common.h、bl_info.c 的冻结契约一致：
 *   页头 16B: magic "PPG1" / seq u16 / state=0xA5 / fmt=1 / data_crc16 / hdr_crc16 / rsvd
 *   数据 0x010~0x099 + 保留 0x09A~0x0FF + BL 槽 0x100~0x10F
 *   data_crc16 覆盖 0x010~0x10F；hdr_crc16 覆盖页头 0x000~0x009
 * 提交目标选择（§7.2 v3.1）：优先无效页（物理序号最小）→ 全有效选 seq 最小。
 * 读取：取有效页中 seq 最大者，不改写任何东西。
 * 守卫：本模块擦/写仅限 3 个参数页，其它地址一律拒绝（防自毁，T-35）。
 */
#include "param_storage.h"
#include "boot_flag.h"   /* boot_crc32（页 8 备份块校验，与 BL/host 同口径）*/
#include "app_fw_version.h"  /* APP_FORCE_METER_COEFF（A6 测试构建开关）*/
#include <string.h>

/* ===== 默认值 ===== */
#define DEF_STD_COND       0
#define DEF_METER_COEFF    1.000f
#define DEF_MEDIUM_COEFF   1.000f
#define DEF_FLOW_UNIT      0
#define DEF_TOTAL_UNIT     0
#define DEF_SMALL_SIGNAL   2.0f
#define DEF_FILTER_TIME    1.0f
#define DEF_FILTER_WINDOW_COUNT 10
#define DEF_SAMPLE_INTERVAL_MS  500
#define DEF_DAMPING_TIME   1.0f
#define DEF_VALUE_4MA      0.0f
#define DEF_VALUE_20MA     100.0f
#define DEF_FREQ_OUTPUT    1000.0f
#define DEF_PULSE_EQUIV    0
#define DEF_MEDIUM_DENSITY 1000.0f
#define DEF_PIPE_DIAMETER  25.0f
#define DEF_GAS_REF_PRESS  101.3f
#define DEF_GAS_REF_TEMP   20.0f
#define DEF_REYNOLDS_K     1.000f
#define DEF_TOTAL_FACTOR   1.000f
#define DEF_PRESET_TOTAL   0.0f
#define DEF_DAC_ZERO       12100u
#define DEF_DAC_FULL       60000u
#define DEF_FORWARD_TOTAL  0.0f
#define DEF_REVERSE_TOTAL  0.0f
#define DEF_MODBUS_ADDR    2
#define DEF_BAUD_RATE      4   /* BAUD_115200, 与 MX_USART2_UART_INIT 硬编码一致 */
#define DEF_PWD_OPERATOR   0
#define DEF_PWD_ENGINEER   123
#define DEF_LANGUAGE       0
#define DEF_OLED_RECOVERY_INTERVAL  50  /* 50 × 100ms = 5s, 0=禁用 */
#define DEF_CAL_ENABLED      0
#define DEF_CAL_K            1.0f
#define DEF_CAL_PCT_0        0.0f
#define DEF_CAL_PCT_1        3.0f
#define DEF_CAL_PCT_2        10.0f
#define DEF_CAL_PCT_3        25.0f
#define DEF_CAL_PCT_4        50.0f
#define DEF_CAL_PCT_5        75.0f
#define DEF_CAL_PCT_6        100.0f

/* ===== 范围限制 ===== */
#define METER_COEFF_MIN    0.001f
#define METER_COEFF_MAX    99.999f
#define MEDIUM_COEFF_MIN   0.100f
#define MEDIUM_COEFF_MAX   10.000f
#define SMALL_SIGNAL_MIN   0.0f
#define SMALL_SIGNAL_MAX   10.0f
#define FILTER_TIME_MIN    0.1f
#define FILTER_TIME_MAX    100.0f
#define FILTER_WINDOW_COUNT_MIN ((uint16_t)2)
#define FILTER_WINDOW_COUNT_MAX ((uint16_t)10)
#define SAMPLE_INTERVAL_MIN_MS  ((uint16_t)100)
#define SAMPLE_INTERVAL_MAX_MS  ((uint16_t)60000)
#define DAMPING_TIME_MIN   0.1f
#define DAMPING_TIME_MAX   100.0f
#define VALUE_4MA_MIN      (-9999.0f)
#define VALUE_4MA_MAX      99999.0f
#define VALUE_20MA_MIN     0.1f
#define VALUE_20MA_MAX     99999.0f
#define FREQ_OUTPUT_MIN    0.0f
#define FREQ_OUTPUT_MAX    10000.0f
#define MEDIUM_DENSITY_MIN 0.001f
#define MEDIUM_DENSITY_MAX 99999.0f
#define PIPE_DIAMETER_MIN  0.1f
#define PIPE_DIAMETER_MAX  99999.0f
#define GAS_REF_PRESS_MIN  0.0f
#define GAS_REF_PRESS_MAX  99999.0f
#define GAS_REF_TEMP_MIN   (-40.0f)
#define GAS_REF_TEMP_MAX   200.0f
#define REYNOLDS_K_MIN     0.001f
#define REYNOLDS_K_MAX     10.000f
#define TOTAL_FACTOR_MIN   0.001f
#define TOTAL_FACTOR_MAX   99.999f
#define PRESET_TOTAL_MIN   0.0f
#define PRESET_TOTAL_MAX   9999999.0f
#define FORWARD_TOTAL_MIN  0.0f
#define FORWARD_TOTAL_MAX  9999999.0f
#define REVERSE_TOTAL_MIN  0.0f
#define REVERSE_TOTAL_MAX  9999999.0f
#define MODBUS_ADDR_MIN    ((uint16_t)1)
#define MODBUS_ADDR_MAX    ((uint16_t)247)
#define OLED_RECOVERY_MIN  ((uint16_t)0)    /* 0 = 禁用自愈 */
#define OLED_RECOVERY_MAX  ((uint16_t)600)  /* 600 × 100ms = 60s */
#define CAL_K_MIN          0.5f
#define CAL_K_MAX          2.0f
#define CAL_PCT_MIN        0.0f
#define CAL_PCT_MAX        100.0f
#define CAL_POINT_COUNT    7

/* ===== Flash 页分区（与 bl_common.h 冻结契约一致）===== */
#define STORE_PAGE1_BASE   0x0800F400u   /* Page 61 */
#define STORE_PAGE2_BASE   0x0800F800u   /* Page 62 */
#define STORE_PAGE3_BASE   0x0800FC00u   /* Page 63 */
#define STORE_PAGE_SIZE    1024u

/* 页内偏移（§7.2 冻结，改动需重新评审）*/
#define PG_OFF_MAGIC       0x000u
#define PG_OFF_SEQ         0x004u
#define PG_OFF_STATE       0x006u         /* 低字节；0x07 高字节 = fmt_ver */
#define PG_OFF_DATA_CRC    0x008u
#define PG_OFF_HDR_CRC     0x00Au
#define PG_OFF_DATA        0x010u
#define PG_OFF_SLOT        0x100u
#define PG_COMMIT_BYTES    0x110u         /* 每次提交编程 0x000~0x10F */

#define PG_MAGIC           0x50504731u    /* "PPG1" */
#define PG_STATE_VALID     0xA5u
#define PG_FMT_VER         0x01u
#define SLOT_MAGIC         0x424C4346u    /* "BLCF" */

/* 数据区字段偏移（§7.1 字段核算，全部冻结）*/
#define D_METER_COEFF      0x010u
#define D_CAL_ENABLED      0x014u
#define D_CAL_K            0x018u         /* 7 × f32 */
#define D_CAL_PCT          0x034u         /* 7 × f32 */
#define D_MEDIUM_COEFF     0x050u
#define D_CFG_F            0x054u         /* 10 × f32 */
#define D_CFG_U16          0x07Cu         /* 4 × u16 */
#define D_CFG_U8           0x084u         /* 6 × u8 */
#define D_PRESET_TOTAL     0x08Au
#define D_DAC_ZERO         0x08Eu
#define D_DAC_FULL         0x090u
#define D_SPAN_4MA         0x092u
#define D_SPAN_20MA        0x096u
#define D_FORWARD_TOTAL    0x09Au

/* ===== 枚举字符串 ===== */
static const char * const s_std_cond_str[STD_COND_COUNT] = {
    "101KPa/0C", "101KPa/20C", "101KPa/25C"
};
static const char * const s_flow_unit_str[FLOW_UNIT_COUNT] = {
    "m3/h", "L/h", "L/min", "kg/h"
};
static const char * const s_total_unit_str[TOTAL_UNIT_COUNT] = {
    "m3", "L", "kg", "t"
};
static const char * const s_pulse_equiv_str[PULSE_EQUIV_COUNT] = {
    "0.001 L/p", "0.01  L/p", "0.1   L/p",
    "1     L/p", "10    L/p", "100   L/p"
};
static const char * const s_baud_rate_str[BAUD_RATE_COUNT] = {
    "4800", "9600", "19200", "38400", "115200", "2400"
};

/* ===== 内部 RAM 缓存 — static ===== */
static param_basic_t s_params;
static uint8_t       s_param_status;      /* PARAM_STATUS_* */
static uint8_t       s_param_dirty;       /* 1 = RAM 已改，待 param_flush() 落盘 */

/* 页镜像暂存（提交编码 / 读取解码共用；模块单线程顺序使用）*/
static uint8_t s_page_img[PG_COMMIT_BYTES];

/* ===== 辅助函数 — static ===== */
static float clamp_f(float val, float lo, float hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

static uint8_t clamp_u8(uint8_t val, uint8_t lo, uint8_t hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

static uint16_t clamp_u16(uint16_t val, uint16_t lo, uint16_t hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/* float 与 uint32_t 互转 (通过 union, 避免 strict-aliasing) */
static uint32_t float_to_u32(float f)
{
    union { float f; uint32_t u; } cvt;
    cvt.f = f;
    return cvt.u;
}

static float u32_to_float(uint32_t u)
{
    union { float f; uint32_t u; } cvt;
    cvt.u = u;
    return cvt.f;
}

/* ===== CRC16-CCITT-FALSE（init 0xFFFF, poly 0x1021, 无反射；
 * 页头/数据/BL 槽三处校验与 Boot/Src/bl_crc.c 同口径）===== */
static uint16_t crc16_ccitt_false(const uint8_t *p, uint32_t len)
{
    uint16_t crc = 0xFFFFu;
    uint32_t i;
    int      b;

    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (b = 0; b < 8; b++)
        {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ===== Flash 原始访问（仅限 3 个参数页，守卫在写入侧）===== */
static const uint32_t s_store_pages[3] = {
    STORE_PAGE1_BASE, STORE_PAGE2_BASE, STORE_PAGE3_BASE
};

static uint16_t flash_rd16(uint32_t addr)
{
    return *(volatile uint16_t *)addr;
}

static uint32_t flash_rd32(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}

/* 搬运 Flash → s_page_img（按半字，避开 32 位未对齐风险）*/
static void flash_read_region(uint32_t base, uint32_t off, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i += 2u)
    {
        uint16_t w = flash_rd16(base + off + i);
        s_page_img[off + i]      = (uint8_t)(w & 0xFFu);
        s_page_img[off + i + 1u] = (uint8_t)((w >> 8) & 0xFFu);
    }
}

/* 页头三条件校验（magic + state + hdr_crc16），可选连带 data_crc16。
 * 通过时输出 seq。字节从 Flash 按半字拼装，与 BL page_header_valid 一致。*/
static int page_header_ok(uint32_t base, int check_data, uint16_t *seq_out)
{
    uint8_t  bytes[12];
    uint32_t i;

    if (flash_rd32(base + PG_OFF_MAGIC) != PG_MAGIC)             { return 0; }
    if ((uint8_t)flash_rd16(base + PG_OFF_STATE) != PG_STATE_VALID) { return 0; }

    for (i = 0; i < 10u; i += 2u)
    {
        uint16_t w = flash_rd16(base + i);
        bytes[i]      = (uint8_t)(w & 0xFFu);
        bytes[i + 1u] = (uint8_t)((w >> 8) & 0xFFu);
    }
    if (crc16_ccitt_false(bytes, 10u) != flash_rd16(base + PG_OFF_HDR_CRC))
    {
        return 0;
    }

    if (check_data)
    {
        uint16_t calc = crc16_ccitt_false(&s_page_img[PG_OFF_DATA],
                                          PG_COMMIT_BYTES - PG_OFF_DATA);
        uint16_t stored = (uint16_t)((uint16_t)s_page_img[PG_OFF_DATA_CRC] |
                                     (uint16_t)((uint16_t)s_page_img[PG_OFF_DATA_CRC + 1u] << 8));
        if (calc != stored)
        {
            return 0;
        }
    }

    if (seq_out != NULL) { *seq_out = flash_rd16(base + PG_OFF_SEQ); }
    return 1;
}

/* 读取路径：3 页中取"页头+数据全有效且 seq 最大"者（u16 回绕用 int16 差值）。
 * 命中时数据区已在 s_page_img，返回页基址；无有效页返回 0。*/
static uint32_t store_pick_read_page(void)
{
    uint32_t best = 0u;
    uint16_t best_seq = 0u;
    int      have_best = 0;
    int      i;

    for (i = 0; i < 3; i++)
    {
        uint16_t seq;
        flash_read_region(s_store_pages[i], 0u, PG_COMMIT_BYTES);
        if (page_header_ok(s_store_pages[i], 1, &seq))
        {
            if (!have_best || ((int16_t)(seq - best_seq) > 0))
            {
                best = s_store_pages[i];
                best_seq = seq;
                have_best = 1;
            }
        }
    }

    if (!have_best)
    {
        return 0u;
    }
    /* 重新搬运最佳页（循环中 s_page_img 可能被后续页覆盖）*/
    flash_read_region(best, 0u, PG_COMMIT_BYTES);
    return best;
}

/* 提交目标选择（§7.2 v3.1）：
 * 1) 存在无效页（任一校验不过，含未初始化）→ 物理序号最小的无效页；
 * 2) 三页全有效 → seq 最小者（int16 差值比较）；
 * 3) seq 相同 → 物理序号最小。
 * 返回目标页基址，并输出 new_seq = 最大有效 seq + 1（无有效页则为 1）。*/
static uint32_t store_pick_commit_target(uint16_t *new_seq_out)
{
    int      valid[3];
    uint16_t seqs[3];
    uint16_t max_seq = 0u;
    int      have_valid = 0;
    int      target = -1;
    int      i;

    for (i = 0; i < 3; i++)
    {
        uint16_t seq = 0u;
        flash_read_region(s_store_pages[i], 0u, PG_COMMIT_BYTES);
        valid[i] = page_header_ok(s_store_pages[i], 1, &seq);
        seqs[i]  = seq;
        if (valid[i] && (!have_valid || ((int16_t)(seq - max_seq) > 0)))
        {
            max_seq = seq;
            have_valid = 1;
        }
    }

    for (i = 0; i < 3; i++)
    {
        if (!valid[i]) { target = i; break; }
    }
    if (target < 0)
    {
        target = 0;
        for (i = 1; i < 3; i++)
        {
            int16_t d = (int16_t)(seqs[i] - seqs[target]);
            if (d < 0) { target = i; }
        }
    }

    *new_seq_out = (uint16_t)(have_valid ? (uint16_t)(max_seq + 1u) : 1u);
    return s_store_pages[target];
}

/* s_params → 页镜像编码（含 BL 槽；data_crc16 / hdr_crc16 随之填好）*/
static void page_image_build(uint16_t seq)
{
    uint32_t i;
    uint16_t crc;

    memset(s_page_img, 0xFF, sizeof(s_page_img));

    /* --- 数据区 0x010~0x099 --- */
    {
        uint32_t v;
        v = float_to_u32(s_params.meter_coeff);
        memcpy(&s_page_img[D_METER_COEFF], &v, 4u);
        v = (uint32_t)(s_params.cal_enabled ? 1u : 0u);
        memcpy(&s_page_img[D_CAL_ENABLED], &v, 4u);
        for (i = 0; i < CAL_POINT_COUNT; i++)
        {
            v = float_to_u32(s_params.cal_k[i]);
            memcpy(&s_page_img[D_CAL_K + i * 4u], &v, 4u);
        }
        for (i = 0; i < CAL_POINT_COUNT; i++)
        {
            v = float_to_u32(s_params.cal_pct[i]);
            memcpy(&s_page_img[D_CAL_PCT + i * 4u], &v, 4u);
        }
        v = float_to_u32(s_params.medium_coeff);
        memcpy(&s_page_img[D_MEDIUM_COEFF], &v, 4u);

        /* K_CFG_F（顺序冻结，§7.1）*/
        {
            const float f[10] = {
                s_params.small_signal, s_params.filter_time,
                s_params.damping_time, s_params.freq_output,
                s_params.medium_density, s_params.pipe_diameter,
                s_params.gas_ref_press, s_params.gas_ref_temp,
                s_params.reynolds_k, s_params.total_factor
            };
            for (i = 0; i < 10u; i++)
            {
                v = float_to_u32(f[i]);
                memcpy(&s_page_img[D_CFG_F + i * 4u], &v, 4u);
            }
        }

        /* K_CFG_U16 */
        {
            const uint16_t h[4] = {
                s_params.filter_window_count, s_params.sample_interval_ms,
                s_params.modbus_addr, s_params.oled_recovery_interval
            };
            for (i = 0; i < 4u; i++)
            {
                memcpy(&s_page_img[D_CFG_U16 + i * 2u], &h[i], 2u);
            }
        }

        /* K_CFG_U8 */
        s_page_img[D_CFG_U8 + 0u] = s_params.std_cond;
        s_page_img[D_CFG_U8 + 1u] = s_params.flow_unit;
        s_page_img[D_CFG_U8 + 2u] = s_params.total_unit;
        s_page_img[D_CFG_U8 + 3u] = s_params.pulse_equiv;
        s_page_img[D_CFG_U8 + 4u] = s_params.uart_config;
        s_page_img[D_CFG_U8 + 5u] = s_params.language;

        v = float_to_u32(s_params.preset_total);
        memcpy(&s_page_img[D_PRESET_TOTAL], &v, 4u);
        memcpy(&s_page_img[D_DAC_ZERO], &s_params.dac_zero, 2u);
        memcpy(&s_page_img[D_DAC_FULL], &s_params.dac_full, 2u);
        v = float_to_u32(s_params.value_4ma);
        memcpy(&s_page_img[D_SPAN_4MA], &v, 4u);
        v = float_to_u32(s_params.value_20ma);
        memcpy(&s_page_img[D_SPAN_20MA], &v, 4u);
        v = float_to_u32(s_params.forward_total);
        memcpy(&s_page_img[D_FORWARD_TOTAL], &v, 4u);
    }

    /* --- BL 通信槽 0x100~0x10F（每次提交都从 RAM 缓存重写, §7.2 v3.1）--- */
    {
        uint32_t m = SLOT_MAGIC;
        memcpy(&s_page_img[PG_OFF_SLOT], &m, 4u);
        s_page_img[PG_OFF_SLOT + 4u] = s_params.uart_config;
        /* 0x105..0x107 保持 0xFF */
        crc = crc16_ccitt_false(&s_page_img[PG_OFF_SLOT], 8u);
        memcpy(&s_page_img[PG_OFF_SLOT + 8u], &crc, 2u);
        /* 0x10A..0x10F 保持 0xFF */
    }

    /* --- 页头（data_crc16 先于 hdr_crc16）--- */
    {
        uint32_t m = PG_MAGIC;
        memcpy(&s_page_img[PG_OFF_MAGIC], &m, 4u);
        memcpy(&s_page_img[PG_OFF_SEQ], &seq, 2u);
        s_page_img[PG_OFF_STATE] = PG_STATE_VALID;
        s_page_img[PG_OFF_STATE + 1u] = PG_FMT_VER;

        crc = crc16_ccitt_false(&s_page_img[PG_OFF_DATA],
                                PG_COMMIT_BYTES - PG_OFF_DATA);
        memcpy(&s_page_img[PG_OFF_DATA_CRC], &crc, 2u);

        crc = crc16_ccitt_false(&s_page_img[0], 10u);
        memcpy(&s_page_img[PG_OFF_HDR_CRC], &crc, 2u);
        /* 0x00C..0x00F rsvd 保持 0xFF */
    }
}

/* 浮点字段解码：NaN/±Inf 位形（0xFF 字段/前向兼容/CRC 碰撞场景）回落默认值，
 * 其余 clamp 到合法区间（审查 A3-#3：clamp_f 对 NaN 全放行的防护）*/
static float dec_f(uint32_t off, float def, float lo, float hi)
{
    uint32_t v;
    memcpy(&v, &s_page_img[off], 4u);
    if ((v & 0x7F800000u) == 0x7F800000u) { return def; }
    return clamp_f(u32_to_float(v), lo, hi);
}

/* 页镜像 → s_params 解码（数据已过 CRC，防御性 clamp + NaN 防护）*/
static void page_image_decode(void)
{
    uint32_t v;
    uint16_t h;
    uint32_t i;

    s_params.meter_coeff = dec_f(D_METER_COEFF, DEF_METER_COEFF,
                                 METER_COEFF_MIN, METER_COEFF_MAX);
    memcpy(&v, &s_page_img[D_CAL_ENABLED], 4u);
    s_params.cal_enabled = (uint8_t)(v & 1u);
    for (i = 0; i < CAL_POINT_COUNT; i++)
    {
        s_params.cal_k[i] = dec_f(D_CAL_K + i * 4u, DEF_CAL_K, CAL_K_MIN, CAL_K_MAX);
    }
    for (i = 0; i < CAL_POINT_COUNT; i++)
    {
        static const float s_pct_def[CAL_POINT_COUNT] =
            { DEF_CAL_PCT_0, DEF_CAL_PCT_1, DEF_CAL_PCT_2, DEF_CAL_PCT_3,
              DEF_CAL_PCT_4, DEF_CAL_PCT_5, DEF_CAL_PCT_6 };
        s_params.cal_pct[i] = dec_f(D_CAL_PCT + i * 4u, s_pct_def[i], CAL_PCT_MIN, CAL_PCT_MAX);
    }

    s_params.medium_coeff = dec_f(D_MEDIUM_COEFF, DEF_MEDIUM_COEFF,
                                  MEDIUM_COEFF_MIN, MEDIUM_COEFF_MAX);

    s_params.small_signal   = dec_f(D_CFG_F + 0u * 4u, DEF_SMALL_SIGNAL, SMALL_SIGNAL_MIN, SMALL_SIGNAL_MAX);
    s_params.filter_time    = dec_f(D_CFG_F + 1u * 4u, DEF_FILTER_TIME, FILTER_TIME_MIN, FILTER_TIME_MAX);
    s_params.damping_time   = dec_f(D_CFG_F + 2u * 4u, DEF_DAMPING_TIME, DAMPING_TIME_MIN, DAMPING_TIME_MAX);
    s_params.freq_output    = dec_f(D_CFG_F + 3u * 4u, DEF_FREQ_OUTPUT, FREQ_OUTPUT_MIN, FREQ_OUTPUT_MAX);
    s_params.medium_density = dec_f(D_CFG_F + 4u * 4u, DEF_MEDIUM_DENSITY, MEDIUM_DENSITY_MIN, MEDIUM_DENSITY_MAX);
    s_params.pipe_diameter  = dec_f(D_CFG_F + 5u * 4u, DEF_PIPE_DIAMETER, PIPE_DIAMETER_MIN, PIPE_DIAMETER_MAX);
    s_params.gas_ref_press  = dec_f(D_CFG_F + 6u * 4u, DEF_GAS_REF_PRESS, GAS_REF_PRESS_MIN, GAS_REF_PRESS_MAX);
    s_params.gas_ref_temp   = dec_f(D_CFG_F + 7u * 4u, DEF_GAS_REF_TEMP, GAS_REF_TEMP_MIN, GAS_REF_TEMP_MAX);
    s_params.reynolds_k     = dec_f(D_CFG_F + 8u * 4u, DEF_REYNOLDS_K, REYNOLDS_K_MIN, REYNOLDS_K_MAX);
    s_params.total_factor   = dec_f(D_CFG_F + 9u * 4u, DEF_TOTAL_FACTOR, TOTAL_FACTOR_MIN, TOTAL_FACTOR_MAX);

    memcpy(&h, &s_page_img[D_CFG_U16 + 0u], 2u);
    s_params.filter_window_count =
        (h == 0u) ? DEF_FILTER_WINDOW_COUNT
                  : clamp_u16(h, FILTER_WINDOW_COUNT_MIN, FILTER_WINDOW_COUNT_MAX);
    memcpy(&h, &s_page_img[D_CFG_U16 + 2u], 2u);
    s_params.sample_interval_ms = clamp_u16(h, SAMPLE_INTERVAL_MIN_MS, SAMPLE_INTERVAL_MAX_MS);
    memcpy(&h, &s_page_img[D_CFG_U16 + 4u], 2u);
    s_params.modbus_addr = clamp_u16(h, MODBUS_ADDR_MIN, MODBUS_ADDR_MAX);
    memcpy(&h, &s_page_img[D_CFG_U16 + 6u], 2u);
    s_params.oled_recovery_interval = clamp_u16(h, OLED_RECOVERY_MIN, OLED_RECOVERY_MAX);

    s_params.std_cond   = clamp_u8(s_page_img[D_CFG_U8 + 0u], 0, (uint8_t)(STD_COND_COUNT - 1));
    s_params.flow_unit  = clamp_u8(s_page_img[D_CFG_U8 + 1u], 0, (uint8_t)(FLOW_UNIT_COUNT - 1));
    s_params.total_unit = clamp_u8(s_page_img[D_CFG_U8 + 2u], 0, (uint8_t)(TOTAL_UNIT_COUNT - 1));
    s_params.pulse_equiv = clamp_u8(s_page_img[D_CFG_U8 + 3u], 0, (uint8_t)(PULSE_EQUIV_COUNT - 1));
    {
        uint8_t raw   = s_page_img[D_CFG_U8 + 4u];
        uint8_t baud  = uart_cfg_baud(raw);
        uint8_t par   = uart_cfg_parity(raw);
        uint8_t stop  = uart_cfg_stop(raw);
        if (baud >= BAUD_RATE_COUNT) baud = DEF_BAUD_RATE;
        if (par >= PARITY_COUNT)     par = PARITY_NONE;
        if (stop >= STOPBITS_COUNT)  stop = STOPBITS_1;
        s_params.uart_config = uart_cfg_pack(baud, par, stop);
    }
    s_params.language = clamp_u8(s_page_img[D_CFG_U8 + 5u], 0, (uint8_t)(LANG_COUNT - 1));

    s_params.preset_total = dec_f(D_PRESET_TOTAL, DEF_PRESET_TOTAL,
                                  PRESET_TOTAL_MIN, PRESET_TOTAL_MAX);
    memcpy(&s_params.dac_zero, &s_page_img[D_DAC_ZERO], 2u);
    memcpy(&s_params.dac_full, &s_page_img[D_DAC_FULL], 2u);
    s_params.value_4ma  = dec_f(D_SPAN_4MA, DEF_VALUE_4MA, VALUE_4MA_MIN, VALUE_4MA_MAX);
    s_params.value_20ma = dec_f(D_SPAN_20MA, DEF_VALUE_20MA, VALUE_20MA_MIN, VALUE_20MA_MAX);
    s_params.forward_total = dec_f(D_FORWARD_TOTAL, DEF_FORWARD_TOTAL,
                                   FORWARD_TOTAL_MIN, FORWARD_TOTAL_MAX);
}

/* 看门狗喂狗（提交期间 Flash 擦写可达数十 ms，且 IWDG 可能带 BL 残余计数在跑）*/
static void feed_iwdg(void)
{
    ((IWDG_TypeDef *)IWDG_BASE)->KR = 0xAAAAu;
}

/* 守卫：目标地址必须是 3 个参数页之一的页基址（T-35，防误擦代码区）*/
static int store_page_guard(uint32_t base)
{
    return (base == STORE_PAGE1_BASE) || (base == STORE_PAGE2_BASE) ||
           (base == STORE_PAGE3_BASE);
}

/* 提交：编码 → 选页 → 擦页 → 半字编程 → 回读校验 */
static HAL_StatusTypeDef param_commit(void)
{
    uint32_t target;
    uint16_t new_seq;
    HAL_StatusTypeDef st;
    FLASH_EraseInitTypeDef ei;
    uint32_t page_error = 0;
    uint32_t i;

    target = store_pick_commit_target(&new_seq);
    if (!store_page_guard(target)) { return HAL_ERROR; }

    page_image_build(new_seq);

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);

    feed_iwdg();
    ei.TypeErase   = FLASH_TYPEERASE_PAGES;
    ei.PageAddress = target;
    ei.NbPages     = 1;
    st = HAL_FLASHEx_Erase(&ei, &page_error);
    feed_iwdg();
    if (st != HAL_OK) { HAL_FLASH_Lock(); return st; }

    for (i = 0; i < PG_COMMIT_BYTES; i += 2u)
    {
        uint16_t w;
        memcpy(&w, &s_page_img[i], 2u);
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, target + i, w);
        if (st != HAL_OK) { HAL_FLASH_Lock(); return st; }
        if ((i & 0x3Fu) == 0u) { feed_iwdg(); }
    }
    HAL_FLASH_Lock();

    /* 回读校验：逐半字比对 + 页有效性（含 data_crc16）*/
    for (i = 0; i < PG_COMMIT_BYTES; i += 2u)
    {
        if (flash_rd16(target + i) != (uint16_t)((uint16_t)s_page_img[i] |
            (uint16_t)((uint16_t)s_page_img[i + 1u] << 8)))
        {
            return HAL_ERROR;
        }
    }
    flash_read_region(target, 0u, PG_COMMIT_BYTES);
    {
        uint16_t seq_r = 0u;
        if (!page_header_ok(target, 1, &seq_r) || (seq_r != new_seq))
        {
            return HAL_ERROR;
        }
    }
    return HAL_OK;
}

/* ===== Public API ===== */

static uint32_t migrate_from_legacy(void);

/* 默认值装载（RAM only，不落盘；init 迁移分支与 reset_defaults 共用）*/
static void param_load_defaults(void)
{
    static const float s_def_pct[CAL_POINT_COUNT] =
        { DEF_CAL_PCT_0, DEF_CAL_PCT_1, DEF_CAL_PCT_2, DEF_CAL_PCT_3,
          DEF_CAL_PCT_4, DEF_CAL_PCT_5, DEF_CAL_PCT_6 };
    int i;

    s_params.std_cond    = DEF_STD_COND;
    s_params.meter_coeff = DEF_METER_COEFF;
    s_params.medium_coeff = DEF_MEDIUM_COEFF;
    s_params.flow_unit   = DEF_FLOW_UNIT;
    s_params.total_unit  = DEF_TOTAL_UNIT;
    s_params.small_signal = DEF_SMALL_SIGNAL;
    s_params.filter_time = DEF_FILTER_TIME;
    s_params.filter_window_count = DEF_FILTER_WINDOW_COUNT;
    s_params.sample_interval_ms = DEF_SAMPLE_INTERVAL_MS;
    s_params.damping_time = DEF_DAMPING_TIME;
    s_params.value_4ma   = DEF_VALUE_4MA;
    s_params.value_20ma  = DEF_VALUE_20MA;
    s_params.freq_output = DEF_FREQ_OUTPUT;
    s_params.pulse_equiv = DEF_PULSE_EQUIV;
    s_params.medium_density = DEF_MEDIUM_DENSITY;
    s_params.pipe_diameter = DEF_PIPE_DIAMETER;
    s_params.gas_ref_press = DEF_GAS_REF_PRESS;
    s_params.gas_ref_temp = DEF_GAS_REF_TEMP;
    s_params.reynolds_k = DEF_REYNOLDS_K;
    s_params.total_factor = DEF_TOTAL_FACTOR;
    s_params.preset_total = DEF_PRESET_TOTAL;
    s_params.dac_zero = DEF_DAC_ZERO;
    s_params.dac_full = DEF_DAC_FULL;
    s_params.forward_total = DEF_FORWARD_TOTAL;
    s_params.reverse_total = DEF_REVERSE_TOTAL;
    s_params.modbus_addr = DEF_MODBUS_ADDR;
    s_params.uart_config = DEF_BAUD_RATE;
    s_params.language = DEF_LANGUAGE;
    s_params.oled_recovery_interval = DEF_OLED_RECOVERY_INTERVAL;
    s_params.cal_enabled = DEF_CAL_ENABLED;
    for (i = 0; i < CAL_POINT_COUNT; i++) s_params.cal_k[i] = DEF_CAL_K;
    for (i = 0; i < CAL_POINT_COUNT; i++) s_params.cal_pct[i] = s_def_pct[i];
    s_params.pwd_operator = DEF_PWD_OPERATOR;
    s_params.pwd_engineer = DEF_PWD_ENGINEER;
}

HAL_StatusTypeDef param_storage_init(void)
{
    uint32_t best;
    HAL_StatusTypeDef st;

    s_param_status = 0u;
    /* 先装默认值再解码：非持久化字段（密码/反向累计总量）与 NaN 回落字段
     * 均依赖此预置（审查 A4：memset 会把 pwd_engineer 清 0 → 鉴权绕过）*/
    param_load_defaults();

    best = store_pick_read_page();
    if (best != 0u)
    {
        page_image_decode();
        s_param_status = PARAM_STATUS_MIGRATED;
        st = HAL_OK;
    }
    else
    {
    /* 无有效新格式页：走旧数据迁移（§7.3 App 路径）——
     * 先把全部数据源读入 RAM（旧 Page 61~63 直读 + 页 8 备份块补缺），
     * 然后才提交新格式（此时旧值已全部在 RAM，擦页安全）。
     * 任一环节掉电，下次上电重跑（页 8 备份不被本模块触碰）。*/
        uint32_t found_mask = migrate_from_legacy();
        if (found_mask == 0x3FFu)            /* 10 组全找到 */
        {
            s_param_status = PARAM_STATUS_MIGRATED;
        }
        else if (found_mask != 0u)
        {
            s_param_status = (uint8_t)(PARAM_STATUS_PARTIAL | PARAM_STATUS_DEFAULTS);
        }
        else
        {
            s_param_status = PARAM_STATUS_DEFAULTS;
        }
        st = param_commit();
    }

    /* A6 双版本测试构建：仅 RAM 覆盖流量系数（编译期常量 0 时整体被优化掉，
     * 不落盘——上位机读 PDU 24/25 即可分辨固件版本）。正式版保持 0.0f。*/
    if (APP_FORCE_METER_COEFF > 0.0f)
    {
        s_params.meter_coeff = clamp_f(APP_FORCE_METER_COEFF,
                                       METER_COEFF_MIN, METER_COEFF_MAX);
    }
    return st;
}

/* ===== 旧数据迁移（§7.3，A4）===== */

/* 旧参数区事实地址：Page 54~63 @ 0x0800D800（不随新分区调整，仅迁移期只读）*/
#define LEGACY_AREA_BASE       0x0800D800u
/* BL 备份页（Page 8，0x08002000）：只读！本模块严禁写入 */
#define BACKUP_PAGE_BASE       0x08002000u
#define BACKUP_MAGIC           0x424C4B31u   /* "BLK1" */
#define BACKUP_BLOB_BYTES      244u
#define BACKUP_PAYLOAD_OFF     0x30u
#define BACKUP_PAYLOAD_BYTES   152u

/* 各组在备份块 payload 内的固定偏移（与 Boot/Inc/legacy_param_read.h LG_OFF_* 一致）*/
static const uint16_t s_lg_off[10] = { 0u, 8u, 20u, 32u, 52u, 68u, 72u, 76u, 140u, 144u };

/* 旧组描述（页序/新代长度/旧代回退/16bit API）——与 Boot legacy_param_read.c s_group_def 一致 */
typedef struct { uint8_t page; uint8_t len; uint8_t len_fb; uint8_t api16; } legacy_def_t;
static const legacy_def_t s_legacy_def[10] = {
    { 54u,  2u, 1u, 0u },   /* 显示/滤波/采样 */
    { 55u,  3u, 0u, 0u },   /* 信号处理 */
    { 56u,  3u, 0u, 0u },   /* 输出配置 */
    { 57u,  5u, 0u, 0u },   /* 介质工况 */
    { 58u,  4u, 0u, 0u },   /* 系统/累积 */
    { 59u,  2u, 0u, 1u },   /* DAC（16bit API）*/
    { 60u,  1u, 0u, 0u },   /* 基本 */
    { 61u, 16u, 1u, 0u },   /* 仪表系数+标定 */
    { 62u,  1u, 0u, 0u },   /* 介质系数 */
    { 63u,  2u, 0u, 0u },   /* Span 量程 */
};

static uint8_t s_backup_blob[BACKUP_BLOB_BYTES];

/* 单组旧记录扫描（语义同 eeprom.c 的 ReadBufferFlash / ReadBufferFlash_16：
 * 槽 = [头标记 0][Len 数据字] 追加式；"本槽头 0 且下一槽首字 0xFF.." 的
 * 第一个槽即最新有效记录。返回 1 并输出实际使用的记录长度。*/
static int legacy_read_group(const legacy_def_t *g, uint32_t *out, uint8_t *len_used)
{
    uint32_t base = LEGACY_AREA_BASE + (uint32_t)(g->page - 54u) * 1024u;
    uint8_t  lens[2];
    uint8_t  n_try = 1u;
    uint8_t  i, k;

    lens[0] = g->len;
    if (g->len_fb != 0u) { lens[1] = g->len_fb; n_try = 2u; }

    for (i = 0; i < n_try; i++)
    {
        uint8_t  len   = lens[i];
        uint32_t words = (uint32_t)len + 1u;              /* 含头标记 */
        uint32_t unit  = g->api16 ? 2u : 4u;
        uint32_t fill  = g->api16 ? (480u / words) : (240u / words);
        uint32_t slot;

        for (slot = 0u; slot < fill; slot++)
        {
            uint32_t addr = base + slot * words * unit;
            uint32_t hdr  = g->api16 ? flash_rd16(addr) : flash_rd32(addr);
            uint32_t next = g->api16 ? flash_rd16(addr + words * unit)
                                     : flash_rd32(addr + words * unit);
            if ((hdr == 0u) && (next == (g->api16 ? 0xFFFFu : 0xFFFFFFFFu)))
            {
                for (k = 0; k < len; k++)
                {
                    out[k] = g->api16 ? (uint32_t)flash_rd16(addr + unit + (uint32_t)k * unit)
                                      : flash_rd32(addr + unit + (uint32_t)k * unit);
                }
                *len_used = len;
                return 1;
            }
        }
    }
    return 0;
}

/* 页 8 备份块解析（只读）：magic/ver/n_groups/crc32 四重校验。
 * 组描述符布局（BL legacy_build_blob）：[0]=page_id、[1]=found?实际len:0、
 * [2]=api16、[3]=0xFF rsvd —— **len==0 即该页无有效记录**。
 * 有效时记录各组实际长度（len>0 = found）。*/
static uint8_t s_backup_len[10];

static int backup_blob_read(void)
{
    uint32_t magic, crc_stored;
    uint32_t a;
    int i;

    for (a = 0u; a < BACKUP_BLOB_BYTES; a += 2u)
    {
        uint16_t w = flash_rd16(BACKUP_PAGE_BASE + a);
        s_backup_blob[a]      = (uint8_t)(w & 0xFFu);
        s_backup_blob[a + 1u] = (uint8_t)((w >> 8) & 0xFFu);
    }

    memcpy(&magic, &s_backup_blob[0], 4u);
    if (magic != BACKUP_MAGIC)          { return 0; }
    if (s_backup_blob[4] != 1u)          { return 0; }   /* ver */
    if (s_backup_blob[6] != 10u)         { return 0; }   /* n_groups */

    memcpy(&crc_stored, &s_backup_blob[240], 4u);
    if (boot_crc32(s_backup_blob, 240u) != crc_stored) { return 0; }

    for (i = 0; i < 10; i++)
    {
        const uint8_t *desc = &s_backup_blob[8u + (uint32_t)i * 4u];
        s_backup_len[i]   = desc[1];      /* 0 = 未找到 */
    }
    return 1;
}

/* 备份块第 i 组的字段读取（0xFFFFFFFF 表示缺失）*/
static uint32_t backup_word(int group, int index)
{
    uint32_t v;
    memcpy(&v, &s_backup_blob[BACKUP_PAYLOAD_OFF + s_lg_off[group] + (uint32_t)index * 4u], 4u);
    return v;
}

/* 旧字段应用辅助：0xFFFFFFFF 或 NaN/Inf 位形跳过（保持默认值），否则 clamp 落位 */
static void apply_f(float *field, uint32_t raw, float lo, float hi)
{
    if (raw == 0xFFFFFFFFu)              { return; }
    if ((raw & 0x7F800000u) == 0x7F800000u) { return; }
    *field = clamp_f(u32_to_float(raw), lo, hi);
}

static void apply_u16(uint16_t *field, uint32_t raw, uint16_t lo, uint16_t hi)
{
    if (raw == 0xFFFFFFFFu || raw == 0xFFFFu) { return; }
    *field = clamp_u16((uint16_t)raw, lo, hi);
}

static void apply_u8(uint8_t *field, uint32_t raw, uint8_t lo, uint8_t hi)
{
    if (raw == 0xFFFFFFFFu || raw == 0xFFFFu) { return; }
    *field = clamp_u8((uint8_t)raw, lo, hi);
}

/* 一组旧字段 → RAM 缓存（w[] 为 Len 个字，缺失字段 0xFFFFFFFF）。
 * 解码语义与旧版 param_storage_init 的逐组读取逻辑一致。*/
static void legacy_apply(uint8_t page, uint8_t len, const uint32_t *w)
{
    switch (page)
    {
        case 54u:   /* 显示/滤波/采样：len=2 [packed, sample]；len=1 旧代 [packed] */
            if (w[0] != 0xFFFFFFFFu)
            {
                uint16_t oled   = (uint16_t)(w[0] & 0xFFFFu);
                uint16_t window = (uint16_t)(w[0] >> 16);
                apply_u16(&s_params.oled_recovery_interval, oled, OLED_RECOVERY_MIN, OLED_RECOVERY_MAX);
                if (window != 0u)
                {
                    s_params.filter_window_count =
                        clamp_u16(window, FILTER_WINDOW_COUNT_MIN, FILTER_WINDOW_COUNT_MAX);
                }
            }
            if (len >= 2u)
            {
                apply_u16(&s_params.sample_interval_ms, w[1],
                          SAMPLE_INTERVAL_MIN_MS, SAMPLE_INTERVAL_MAX_MS);
            }
            break;

        case 55u:
            apply_f(&s_params.small_signal, w[0], SMALL_SIGNAL_MIN, SMALL_SIGNAL_MAX);
            apply_f(&s_params.filter_time,  w[1], FILTER_TIME_MIN, FILTER_TIME_MAX);
            apply_f(&s_params.damping_time, w[2], DAMPING_TIME_MIN, DAMPING_TIME_MAX);
            break;

        case 56u:
            apply_f(&s_params.freq_output, w[0], FREQ_OUTPUT_MIN, FREQ_OUTPUT_MAX);
            apply_u8(&s_params.pulse_equiv, w[1], 0, (uint8_t)(PULSE_EQUIV_COUNT - 1));
            apply_u8(&s_params.language,    w[2], 0, (uint8_t)(LANG_COUNT - 1));
            break;

        case 57u:
            apply_f(&s_params.medium_density, w[0], MEDIUM_DENSITY_MIN, MEDIUM_DENSITY_MAX);
            apply_f(&s_params.pipe_diameter,  w[1], PIPE_DIAMETER_MIN, PIPE_DIAMETER_MAX);
            apply_f(&s_params.gas_ref_press,  w[2], GAS_REF_PRESS_MIN, GAS_REF_PRESS_MAX);
            apply_f(&s_params.gas_ref_temp,   w[3], GAS_REF_TEMP_MIN, GAS_REF_TEMP_MAX);
            apply_f(&s_params.reynolds_k,     w[4], REYNOLDS_K_MIN, REYNOLDS_K_MAX);
            break;

        case 58u:
            apply_u16(&s_params.modbus_addr, w[0], MODBUS_ADDR_MIN, MODBUS_ADDR_MAX);
            if (w[1] != 0xFFFFFFFFu)
            {
                uint8_t raw  = (uint8_t)w[1];
                uint8_t baud = uart_cfg_baud(raw);
                uint8_t par  = uart_cfg_parity(raw);
                uint8_t stop = uart_cfg_stop(raw);
                if ((baud < BAUD_RATE_COUNT) && (par < PARITY_COUNT) &&
                    (stop < STOPBITS_COUNT) && ((raw & 0xC0u) == 0u))
                {
                    s_params.uart_config = raw;
                }
            }
            apply_f(&s_params.total_factor, w[2], TOTAL_FACTOR_MIN, TOTAL_FACTOR_MAX);
            apply_f(&s_params.preset_total, w[3], PRESET_TOTAL_MIN, PRESET_TOTAL_MAX);
            break;

        case 59u:   /* DAC：16bit API，w[] 为两个半字 */
            apply_u16(&s_params.dac_zero, w[0], 0u, 65535u);
            apply_u16(&s_params.dac_full, w[1], 0u, 65535u);
            break;

        case 60u:
            if (w[0] != 0xFFFFFFFFu)
            {
                apply_u8(&s_params.std_cond,   (w[0] >> 16) & 0xFFu, 0, (uint8_t)(STD_COND_COUNT - 1));
                apply_u8(&s_params.flow_unit,  (w[0] >> 8)  & 0xFFu, 0, (uint8_t)(FLOW_UNIT_COUNT - 1));
                apply_u8(&s_params.total_unit, w[0]        & 0xFFu, 0, (uint8_t)(TOTAL_UNIT_COUNT - 1));
            }
            break;

        case 61u:   /* 仪表系数+标定：len=16 新代全字段；len=1 旧代仅 meter_coeff */
            apply_f(&s_params.meter_coeff, w[0], METER_COEFF_MIN, METER_COEFF_MAX);
            if (len >= 16u)
            {
                if (w[1] != 0xFFFFFFFFu) { s_params.cal_enabled = (uint8_t)(w[1] & 1u); }
                {
                    int i;
                    for (i = 0; i < CAL_POINT_COUNT; i++)
                    {
                        apply_f(&s_params.cal_k[i], w[2u + (uint32_t)i], CAL_K_MIN, CAL_K_MAX);
                    }
                    for (i = 0; i < CAL_POINT_COUNT; i++)
                    {
                        apply_f(&s_params.cal_pct[i], w[9u + (uint32_t)i], CAL_PCT_MIN, CAL_PCT_MAX);
                    }
                }
            }
            break;

        case 62u:
            apply_f(&s_params.medium_coeff, w[0], MEDIUM_COEFF_MIN, MEDIUM_COEFF_MAX);
            break;

        case 63u:
            apply_f(&s_params.value_4ma,  w[0], VALUE_4MA_MIN, VALUE_4MA_MAX);
            apply_f(&s_params.value_20ma, w[1], VALUE_20MA_MIN, VALUE_20MA_MAX);
            break;

        default:
            break;
    }
}

/* 迁移编排：返回找到数据的组位图（bit0=组 54 ... bit9=组 63）。
 * 数据源优先级：旧页 61~63 直读（最新且物理存活）> 页 8 备份块（BL 建立，
 * 已过区间校验）> 默认值（param_load_defaults 已预置）。*/
static uint32_t migrate_from_legacy(void)
{
    uint32_t grp[16];
    uint32_t found_mask = 0u;
    int      blob_ok;
    int      i;

    param_load_defaults();
    blob_ok = backup_blob_read();

    for (i = 0; i < 10; i++)
    {
        const legacy_def_t *g = &s_legacy_def[i];
        uint8_t len_used = 0u;
        int applied = 0;

        /* 直读仅限页 61~63：54~60 已落入新 App 代码区，扫描代码字节有误匹配风险，
         * 其值一律走备份块（§7.3 App 路径）*/
        if ((g->page >= 61u) && legacy_read_group(g, grp, &len_used))
        {
            legacy_apply(g->page, len_used, grp);
            applied = 1;
        }
        else if (blob_ok && (s_backup_len[i] != 0u))
        {
            uint8_t k;
            uint8_t n = s_backup_len[i];
            if (n > 16u) { n = 16u; }

            if (g->api16)
            {
                /* DAC 组：备份 payload 按 BL legacy_param_read.c 语义连排两个半字
                 * （4B 内低 16 位 = zero、高 16 位 = full）*/
                uint32_t w = backup_word(i, 0);
                grp[0] = w & 0xFFFFu;
                grp[1] = (w >> 16) & 0xFFFFu;
                for (k = 2u; k < 16u; k++) { grp[k] = 0xFFFFFFFFu; }
            }
            else
            {
                for (k = 0; k < n; k++)  { grp[k] = backup_word(i, k); }
                for (; k < 16u; k++)     { grp[k] = 0xFFFFFFFFu; }
            }
            legacy_apply(g->page, n, grp);
            applied = 1;
        }

        if (applied) { found_mask |= (1u << i); }
    }

    return found_mask;
}


HAL_StatusTypeDef param_storage_get_basic(const param_basic_t **pp_out)
{
    if (!pp_out) return HAL_ERROR;
    *pp_out = &s_params;
    return HAL_OK;
}

/* 有脏数据时才整页提交；一次 flush 覆盖此前所有 setter 修改（批量）。
 * 只能主循环调用（含 HAL_FLASH 擦写，禁入 ISR）。*/
HAL_StatusTypeDef param_flush(void)
{
    HAL_StatusTypeDef st;
    if (!s_param_dirty) return HAL_OK;
    st = param_commit();
    if (st == HAL_OK) s_param_dirty = 0u;
    return st;
}

uint8_t param_get_status(void)
{
    return s_param_status;
}

/* ===== Phase 1 getter ===== */
uint8_t  param_get_std_cond(void)     { return s_params.std_cond; }
float    param_get_meter_coeff(void)  { return s_params.meter_coeff; }
float    param_get_medium_coeff(void) { return s_params.medium_coeff; }
uint8_t  param_get_flow_unit(void)    { return s_params.flow_unit; }
uint8_t  param_get_total_unit(void)   { return s_params.total_unit; }
float    param_get_small_signal(void) { return s_params.small_signal; }
float    param_get_filter_time(void)  { return s_params.filter_time; }
float    param_get_damping_time(void) { return s_params.damping_time; }
uint16_t param_get_pwd_operator(void) { return s_params.pwd_operator; }

/* ===== Phase 2 输出 getter ===== */
float    param_get_value_4ma(void)    { return s_params.value_4ma; }
float    param_get_value_20ma(void)   { return s_params.value_20ma; }
float    param_get_freq_output(void)  { return s_params.freq_output; }
uint8_t  param_get_pulse_equiv(void)  { return s_params.pulse_equiv; }

/* ===== Phase 2 介质/工况 getter ===== */
float    param_get_medium_density(void) { return s_params.medium_density; }
float    param_get_pipe_diameter(void)  { return s_params.pipe_diameter; }
float    param_get_gas_ref_press(void)  { return s_params.gas_ref_press; }
float    param_get_gas_ref_temp(void)   { return s_params.gas_ref_temp; }
float    param_get_reynolds_k(void)     { return s_params.reynolds_k; }

/* ===== Phase 2 累积器 getter ===== */
float    param_get_total_factor(void) { return s_params.total_factor; }
float    param_get_preset_total(void) { return s_params.preset_total; }

/* ===== Phase 3 累计总量 getter ===== */
float    param_get_forward_total(void) { return s_params.forward_total; }
float    param_get_reverse_total(void) { return s_params.reverse_total; }

/* ===== Phase 4 系统 getter ===== */
uint16_t param_get_modbus_addr(void) { return s_params.modbus_addr; }
uint8_t  param_get_baud_rate(void)   { return uart_cfg_baud(s_params.uart_config); }
uint8_t  param_get_uart_config(void) { return s_params.uart_config; }
uint16_t param_get_pwd_engineer(void) { return s_params.pwd_engineer; }
uint8_t  param_get_language(void)    { return s_params.language; }

/* ===== Phase 5 OLED 自愈 getter ===== */
uint16_t param_get_oled_recovery_interval(void) { return s_params.oled_recovery_interval; }

/* ===== 瞬时流量滤波 getter ===== */
uint16_t param_get_filter_window_count(void) { return s_params.filter_window_count; }
uint16_t param_get_sample_interval_ms(void) { return s_params.sample_interval_ms; }

/* ===== DAC / Span getter ===== */
uint16_t param_get_dac_zero(void) { return s_params.dac_zero; }
uint16_t param_get_dac_full(void) { return s_params.dac_full; }

/* ===== Phase 1 setter ===== */
/* 变更检测: 钳位后与当前值一致则不触发 Flash 擦写。
 * float 经 float_to_u32 位级比较（NaN/±0 与 memcmp 同口径）。*/
HAL_StatusTypeDef param_set_std_cond(uint8_t idx)
{
    uint8_t new_v = clamp_u8(idx, 0, (uint8_t)(STD_COND_COUNT - 1));
    if (new_v == s_params.std_cond) return HAL_OK;
    s_params.std_cond = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_flow_unit(uint8_t idx)
{
    uint8_t new_v = clamp_u8(idx, 0, (uint8_t)(FLOW_UNIT_COUNT - 1));
    if (new_v == s_params.flow_unit) return HAL_OK;
    s_params.flow_unit = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_total_unit(uint8_t idx)
{
    uint8_t new_v = clamp_u8(idx, 0, (uint8_t)(TOTAL_UNIT_COUNT - 1));
    if (new_v == s_params.total_unit) return HAL_OK;
    s_params.total_unit = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_meter_coeff(float val)
{
    float new_v = clamp_f(val, METER_COEFF_MIN, METER_COEFF_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.meter_coeff)) return HAL_OK;
    s_params.meter_coeff = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_medium_coeff(float val)
{
    float new_v = clamp_f(val, MEDIUM_COEFF_MIN, MEDIUM_COEFF_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.medium_coeff)) return HAL_OK;
    s_params.medium_coeff = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_small_signal(float val)
{
    float new_v = clamp_f(val, SMALL_SIGNAL_MIN, SMALL_SIGNAL_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.small_signal)) return HAL_OK;
    s_params.small_signal = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_filter_time(float val)
{
    float new_v = clamp_f(val, FILTER_TIME_MIN, FILTER_TIME_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.filter_time)) return HAL_OK;
    s_params.filter_time = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_damping_time(float val)
{
    float new_v = clamp_f(val, DAMPING_TIME_MIN, DAMPING_TIME_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.damping_time)) return HAL_OK;
    s_params.damping_time = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

/* ===== Phase 2 输出 setter ===== */
/* value_4ma / value_20ma: 仅 RAM（启动同步与分半写缓冲），落盘走 param_set_span_values */
HAL_StatusTypeDef param_set_value_4ma(float val)
{
    s_params.value_4ma = clamp_f(val, VALUE_4MA_MIN, VALUE_4MA_MAX);
    return HAL_OK;
}

HAL_StatusTypeDef param_set_value_20ma(float val)
{
    s_params.value_20ma = clamp_f(val, VALUE_20MA_MIN, VALUE_20MA_MAX);
    return HAL_OK;
}

HAL_StatusTypeDef param_set_span_values(float lo, float hi)
{
    float new_lo = clamp_f(lo, VALUE_4MA_MIN, VALUE_4MA_MAX);
    float new_hi = clamp_f(hi, VALUE_20MA_MIN, VALUE_20MA_MAX);
    if ((float_to_u32(new_lo) == float_to_u32(s_params.value_4ma)) &&
        (float_to_u32(new_hi) == float_to_u32(s_params.value_20ma))) return HAL_OK;
    s_params.value_4ma = new_lo;
    s_params.value_20ma = new_hi;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_freq_output(float val)
{
    float new_v = clamp_f(val, FREQ_OUTPUT_MIN, FREQ_OUTPUT_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.freq_output)) return HAL_OK;
    s_params.freq_output = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_pulse_equiv(uint8_t idx)
{
    uint8_t new_v = clamp_u8(idx, 0, (uint8_t)(PULSE_EQUIV_COUNT - 1));
    if (new_v == s_params.pulse_equiv) return HAL_OK;
    s_params.pulse_equiv = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

/* ===== Phase 2 介质/工况 setter ===== */
HAL_StatusTypeDef param_set_medium_density(float val)
{
    float new_v = clamp_f(val, MEDIUM_DENSITY_MIN, MEDIUM_DENSITY_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.medium_density)) return HAL_OK;
    s_params.medium_density = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_pipe_diameter(float val)
{
    float new_v = clamp_f(val, PIPE_DIAMETER_MIN, PIPE_DIAMETER_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.pipe_diameter)) return HAL_OK;
    s_params.pipe_diameter = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_gas_ref_press(float val)
{
    float new_v = clamp_f(val, GAS_REF_PRESS_MIN, GAS_REF_PRESS_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.gas_ref_press)) return HAL_OK;
    s_params.gas_ref_press = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_gas_ref_temp(float val)
{
    float new_v = clamp_f(val, GAS_REF_TEMP_MIN, GAS_REF_TEMP_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.gas_ref_temp)) return HAL_OK;
    s_params.gas_ref_temp = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_reynolds_k(float val)
{
    float new_v = clamp_f(val, REYNOLDS_K_MIN, REYNOLDS_K_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.reynolds_k)) return HAL_OK;
    s_params.reynolds_k = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

/* ===== Phase 2 累积器 setter ===== */
HAL_StatusTypeDef param_set_total_factor(float val)
{
    float new_v = clamp_f(val, TOTAL_FACTOR_MIN, TOTAL_FACTOR_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.total_factor)) return HAL_OK;
    s_params.total_factor = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_preset_total(float val)
{
    float new_v = clamp_f(val, PRESET_TOTAL_MIN, PRESET_TOTAL_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.preset_total)) return HAL_OK;
    s_params.preset_total = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

/* ===== Phase 3 累计总量 setter ===== */
HAL_StatusTypeDef param_set_forward_total(float val)
{
    float new_v = clamp_f(val, FORWARD_TOTAL_MIN, FORWARD_TOTAL_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.forward_total)) return HAL_OK;
    s_params.forward_total = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

/* 反向累计总量频繁变化，当前仍不持久化，断电后重置。 */
HAL_StatusTypeDef param_set_reverse_total(float val)
{
    s_params.reverse_total = clamp_f(val, REVERSE_TOTAL_MIN, REVERSE_TOTAL_MAX);
    return HAL_OK;
}

/* ===== DAC setter (一次提交两条) ===== */
HAL_StatusTypeDef param_set_dac_values(uint16_t zero, uint16_t full)
{
    if ((zero == s_params.dac_zero) && (full == s_params.dac_full)) return HAL_OK;
    s_params.dac_zero = zero;
    s_params.dac_full = full;
    s_param_dirty = 1u;
    return HAL_OK;
}

/* ===== Phase 4 系统 setter ===== */
HAL_StatusTypeDef param_set_modbus_addr(uint16_t addr)
{
    uint16_t new_v = clamp_u16(addr, MODBUS_ADDR_MIN, MODBUS_ADDR_MAX);
    if (new_v == s_params.modbus_addr) return HAL_OK;
    s_params.modbus_addr = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_baud_rate(uint8_t idx)
{
    uint8_t new_cfg = (s_params.uart_config & 0x38u) |
                      (uint8_t)clamp_u8(idx, 0, (uint8_t)(BAUD_RATE_COUNT - 1));
    if (new_cfg == s_params.uart_config) return HAL_OK;
    s_params.uart_config = new_cfg;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_uart_config(uint8_t cfg)
{
    uint8_t baud   = uart_cfg_baud(cfg);
    uint8_t parity = uart_cfg_parity(cfg);
    uint8_t stop   = uart_cfg_stop(cfg);
    if (baud >= BAUD_RATE_COUNT) return HAL_ERROR;
    if (parity >= PARITY_COUNT)  return HAL_ERROR;
    if (stop >= STOPBITS_COUNT)  return HAL_ERROR;
    if (cfg & 0xC0u)            return HAL_ERROR;
    if (cfg == s_params.uart_config) return HAL_OK;
    s_params.uart_config = cfg;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_language(uint8_t idx)
{
    uint8_t new_v = clamp_u8(idx, 0, (uint8_t)(LANG_COUNT - 1));
    if (new_v == s_params.language) return HAL_OK;
    s_params.language = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

/* ===== Phase 5 OLED 自愈 setter ===== */
HAL_StatusTypeDef param_set_oled_recovery_interval(uint16_t val)
{
    uint16_t new_v = clamp_u16(val, OLED_RECOVERY_MIN, OLED_RECOVERY_MAX);
    if (new_v == s_params.oled_recovery_interval) return HAL_OK;
    s_params.oled_recovery_interval = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

/* ===== 瞬时流量滤波 setter ===== */
HAL_StatusTypeDef param_set_filter_window_count(uint16_t val)
{
    uint16_t new_v = clamp_u16(val, FILTER_WINDOW_COUNT_MIN, FILTER_WINDOW_COUNT_MAX);
    if (new_v == s_params.filter_window_count) return HAL_OK;
    s_params.filter_window_count = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_sample_interval_ms(uint16_t val)
{
    uint16_t new_v = clamp_u16(val, SAMPLE_INTERVAL_MIN_MS, SAMPLE_INTERVAL_MAX_MS);
    if (new_v == s_params.sample_interval_ms) return HAL_OK;
    s_params.sample_interval_ms = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

/* ===== Phase 6 标定 getter ===== */
uint8_t  param_get_cal_enabled(void)          { return s_params.cal_enabled; }
float    param_get_cal_k(uint8_t index)       { return (index < CAL_POINT_COUNT) ? s_params.cal_k[index] : 1.0f; }
float    param_get_cal_pct(uint8_t index)     { return (index < CAL_POINT_COUNT) ? s_params.cal_pct[index] : 0.0f; }

/* ===== Phase 6 标定 setter ===== */
HAL_StatusTypeDef param_set_cal_enabled(uint8_t val)
{
    uint8_t new_v = (uint8_t)((val) ? 1 : 0);
    if (new_v == s_params.cal_enabled) return HAL_OK;
    s_params.cal_enabled = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_cal_k(uint8_t index, float val)
{
    float new_v;
    if (index >= CAL_POINT_COUNT) return HAL_ERROR;
    new_v = clamp_f(val, CAL_K_MIN, CAL_K_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.cal_k[index])) return HAL_OK;
    s_params.cal_k[index] = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

HAL_StatusTypeDef param_set_cal_pct(uint8_t index, float val)
{
    float new_v;
    if (index >= CAL_POINT_COUNT) return HAL_ERROR;
    new_v = clamp_f(val, CAL_PCT_MIN, CAL_PCT_MAX);
    if (float_to_u32(new_v) == float_to_u32(s_params.cal_pct[index])) return HAL_OK;
    s_params.cal_pct[index] = new_v;
    s_param_dirty = 1u;
    return HAL_OK;
}

/* ===== 枚举字符串 ===== */
const char *param_get_std_cond_str(uint8_t idx)
{
    if (idx >= STD_COND_COUNT) return s_std_cond_str[0];
    return s_std_cond_str[idx];
}

const char *param_get_flow_unit_str(uint8_t idx)
{
    if (idx >= FLOW_UNIT_COUNT) return s_flow_unit_str[0];
    return s_flow_unit_str[idx];
}

const char *param_get_total_unit_str(uint8_t idx)
{
    if (idx >= TOTAL_UNIT_COUNT) return s_total_unit_str[0];
    return s_total_unit_str[idx];
}

const char * const *param_get_std_cond_strings(void)   { return s_std_cond_str; }
const char * const *param_get_flow_unit_strings(void)  { return s_flow_unit_str; }
const char * const *param_get_total_unit_strings(void) { return s_total_unit_str; }
const char * const *param_get_pulse_equiv_strings(void) { return s_pulse_equiv_str; }
const char * const *param_get_baud_rate_strings(void)   { return s_baud_rate_str; }

/* ===== 恢复出厂默认值 ===== */
HAL_StatusTypeDef param_storage_reset_defaults(void)
{
    param_load_defaults();
    s_param_dirty = 1u;   /* 全量默认值覆盖：置脏走统一 flush，失败可重试 */
    return param_flush();
}
