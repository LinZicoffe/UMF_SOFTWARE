/**
 * @file    param_storage.c
 * @brief   参数存储实现 — RAM 缓存 + Flash 持久化
 */
#include "param_storage.h"
#include "eeprom.h"       /* WriteBufferFlash / ReadBufferFlash */
#include <string.h>

/* ===== 默认值 ===== */
#define DEF_STD_COND       0
#define DEF_METER_COEFF    1.000f
#define DEF_MEDIUM_COEFF   1.000f
#define DEF_FLOW_UNIT      0
#define DEF_TOTAL_UNIT     0
#define DEF_SMALL_SIGNAL   2.0f
#define DEF_FILTER_TIME    1.0f
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

/* Flash 页分配
 * Page 59: DAC 零点/满度 (由 main.h DAC_FLASH_PAGE_ADDR 定义)
 * Page 60: 基本参数 (std_cond + flow_unit + total_unit)
 * Page 61: 仪表系数 (meter_coeff)
 * Page 62: 介质系数 (medium_coeff)
 * Page 63: Span 量程 (由 bsp_usart/bsp_menu 直接管理)
 *
 * 新增分组 (Page 55~58):
 * Page 55: 信号处理组 [small_signal, filter_time, damping_time]
 * Page 56: 输出配置组 [freq_output, pulse_equiv, language]
 * Page 57: 介质工况组 [medium_density, pipe_diameter, gas_ref_press, gas_ref_temp, reynolds_k]
 * Page 58: 系统/累积组 [modbus_addr, uart_config, total_factor, preset_total]
 *
 * Phase 5 新增分组 (Page 54):
 * Page 54: 显示与 OLED 抗干扰组 [oled_recovery_interval]
 *          (独立成页, 避免与 system_group 槽位长度耦合, 兼容旧固件 Flash 数据)
 */
#define PARAM_PAGE_BASIC         ADDR_FLASH_PAGE_60
#define PARAM_PAGE_METER         ADDR_FLASH_PAGE_61
#define PARAM_PAGE_MEDIUM        ADDR_FLASH_PAGE_62
#define PARAM_PAGE_SIGNAL        ADDR_FLASH_PAGE_55
#define PARAM_PAGE_OUTPUT        ADDR_FLASH_PAGE_56
#define PARAM_PAGE_MEDIUM_PARAM  ADDR_FLASH_PAGE_57
#define PARAM_PAGE_SYSTEM        ADDR_FLASH_PAGE_58
#define PARAM_PAGE_DISPLAY       ADDR_FLASH_PAGE_54

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

/* ===== 分组 Flash 写入辅助 — static ===== */

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

/* 信号处理组: [small_signal, filter_time, damping_time] */
static HAL_StatusTypeDef flush_signal_group(void)
{
    uint32_t buf[3];
    buf[0] = float_to_u32(s_params.small_signal);
    buf[1] = float_to_u32(s_params.filter_time);
    buf[2] = float_to_u32(s_params.damping_time);
    return (HAL_StatusTypeDef)WriteBufferFlash(3, PARAM_PAGE_SIGNAL, buf);
}

/* 输出配置组: [freq_output, pulse_equiv, language] */
static HAL_StatusTypeDef flush_output_group(void)
{
    uint32_t buf[3];
    buf[0] = float_to_u32(s_params.freq_output);
    buf[1] = (uint32_t)s_params.pulse_equiv;
    buf[2] = (uint32_t)s_params.language;
    return (HAL_StatusTypeDef)WriteBufferFlash(3, PARAM_PAGE_OUTPUT, buf);
}

/* 介质工况组: [medium_density, pipe_diameter, gas_ref_press, gas_ref_temp, reynolds_k] */
static HAL_StatusTypeDef flush_medium_param_group(void)
{
    uint32_t buf[5];
    buf[0] = float_to_u32(s_params.medium_density);
    buf[1] = float_to_u32(s_params.pipe_diameter);
    buf[2] = float_to_u32(s_params.gas_ref_press);
    buf[3] = float_to_u32(s_params.gas_ref_temp);
    buf[4] = float_to_u32(s_params.reynolds_k);
    return (HAL_StatusTypeDef)WriteBufferFlash(5, PARAM_PAGE_MEDIUM_PARAM, buf);
}

/* 系统/累积组: [modbus_addr, uart_config, total_factor, preset_total]
 * 注意: 字段长度固定为 4, 切勿扩展 (WriteBufferFlash 是链表式追加存储,
 *       槽位大小 = (Len+1)*4 字节, 改 Len 会让旧设备的 Flash 数据无法解码). */
static HAL_StatusTypeDef flush_system_group(void)
{
    uint32_t buf[4];
    buf[0] = (uint32_t)s_params.modbus_addr;
    buf[1] = (uint32_t)s_params.uart_config;
    buf[2] = float_to_u32(s_params.total_factor);
    buf[3] = float_to_u32(s_params.preset_total);
    return (HAL_StatusTypeDef)WriteBufferFlash(4, PARAM_PAGE_SYSTEM, buf);
}

/* 显示/OLED 抗干扰组: [oled_recovery_interval]
 * 独立成页避免与 system_group 槽位长度耦合 (Phase 5 新增). */
static HAL_StatusTypeDef flush_display_group(void)
{
    uint32_t buf[1];
    buf[0] = (uint32_t)s_params.oled_recovery_interval;
    return (HAL_StatusTypeDef)WriteBufferFlash(1, PARAM_PAGE_DISPLAY, buf);
}

/* 仪表系数 + 标定合并组: [meter_coeff, cal_enabled, cal_k[0..6], cal_pct[0..6]]
 * Page 61 Len=16, 每条记录 68 字节, fillcount=14.
 * 所有 setter 共享此函数, 任一字段变更 → 重写整条记录. */
static HAL_StatusTypeDef flush_meter_cal_group(void)
{
    uint32_t buf[16];
    int i;
    buf[0] = float_to_u32(s_params.meter_coeff);
    buf[1] = (uint32_t)s_params.cal_enabled;
    for (i = 0; i < CAL_POINT_COUNT; i++)
        buf[2 + i] = float_to_u32(s_params.cal_k[i]);
    for (i = 0; i < CAL_POINT_COUNT; i++)
        buf[9 + i] = float_to_u32(s_params.cal_pct[i]);
    return (HAL_StatusTypeDef)WriteBufferFlash(16, PARAM_PAGE_METER, buf);
}

/* ===== Public API ===== */

HAL_StatusTypeDef param_storage_init(void)
{
    uint32_t buf;

    /* 读取 Page 60: 打包格式 [23:16]=std_cond, [15:8]=flow_unit, [7:0]=total_unit */
    ReadBufferFlash(1, PARAM_PAGE_BASIC, &buf);
    if (buf == 0xFFFFFFFF) {
        s_params.std_cond   = DEF_STD_COND;
        s_params.flow_unit  = DEF_FLOW_UNIT;
        s_params.total_unit = DEF_TOTAL_UNIT;
    } else {
        s_params.std_cond   = (uint8_t)((buf >> 16) & 0xFF);
        s_params.flow_unit  = (uint8_t)((buf >> 8) & 0xFF);
        s_params.total_unit = (uint8_t)(buf & 0xFF);
        s_params.std_cond   = clamp_u8(s_params.std_cond, 0, (uint8_t)(STD_COND_COUNT - 1));
        s_params.flow_unit  = clamp_u8(s_params.flow_unit, 0, (uint8_t)(FLOW_UNIT_COUNT - 1));
        s_params.total_unit = clamp_u8(s_params.total_unit, 0, (uint8_t)(TOTAL_UNIT_COUNT - 1));
    }

    /* 读取 Page 61: 仪表系数 + 标定合并组 (Len=16)
     * 向后兼容: 先尝试新格式 (Len=16), 若全空则尝试旧格式 (Len=1) 并迁移 */
    {
        static const float s_def_pct[CAL_POINT_COUNT] =
            { DEF_CAL_PCT_0, DEF_CAL_PCT_1, DEF_CAL_PCT_2, DEF_CAL_PCT_3,
              DEF_CAL_PCT_4, DEF_CAL_PCT_5, DEF_CAL_PCT_6 };
        uint32_t new_buf[16];
        int i;
        int all_empty = 1;

        ReadBufferFlash(16, PARAM_PAGE_METER, new_buf);

        /* 检查是否全空 (未写入过新格式) */
        for (i = 0; i < 16; i++) {
            if (new_buf[i] != 0xFFFFFFFFu) { all_empty = 0; break; }
        }

        if (!all_empty) {
            /* 新格式有效, 直接解析 */
            s_params.meter_coeff = clamp_f(u32_to_float(new_buf[0]), METER_COEFF_MIN, METER_COEFF_MAX);
            s_params.cal_enabled = (new_buf[1] == 0xFFFFFFFFu) ? DEF_CAL_ENABLED :
                                   (uint8_t)(new_buf[1] & 1);
            for (i = 0; i < CAL_POINT_COUNT; i++) {
                s_params.cal_k[i] = (new_buf[2 + i] == 0xFFFFFFFFu) ? DEF_CAL_K :
                                    clamp_f(u32_to_float(new_buf[2 + i]), CAL_K_MIN, CAL_K_MAX);
            }
            for (i = 0; i < CAL_POINT_COUNT; i++) {
                s_params.cal_pct[i] = (new_buf[9 + i] == 0xFFFFFFFFu) ? s_def_pct[i] :
                                      clamp_f(u32_to_float(new_buf[9 + i]), CAL_PCT_MIN, CAL_PCT_MAX);
            }
        } else {
            /* 尝试旧格式: Len=1, 仅 meter_coeff */
            uint32_t old_buf;
            ReadBufferFlash(1, PARAM_PAGE_METER, &old_buf);
            if (old_buf != 0xFFFFFFFFu) {
                /* 旧格式有效: 保留 meter_coeff, 标定使用默认值 */
                s_params.meter_coeff = clamp_f(u32_to_float(old_buf), METER_COEFF_MIN, METER_COEFF_MAX);
            } else {
                s_params.meter_coeff = DEF_METER_COEFF;
            }
            s_params.cal_enabled = DEF_CAL_ENABLED;
            for (i = 0; i < CAL_POINT_COUNT; i++) s_params.cal_k[i] = DEF_CAL_K;
            for (i = 0; i < CAL_POINT_COUNT; i++) s_params.cal_pct[i] = s_def_pct[i];
            /* 写入新格式完成迁移 */
            flush_meter_cal_group();
        }
    }

    /* 读取 Page 62: medium_coeff */
    ReadBufferFlash(1, PARAM_PAGE_MEDIUM, &buf);
    s_params.medium_coeff = (buf == 0xFFFFFFFF) ? DEF_MEDIUM_COEFF : u32_to_float(buf);
    s_params.medium_coeff = clamp_f(s_params.medium_coeff, MEDIUM_COEFF_MIN, MEDIUM_COEFF_MAX);

    /* 读取信号处理组 (Page 55) */
    {
        uint32_t buf[3];
        ReadBufferFlash(3, PARAM_PAGE_SIGNAL, buf);
        s_params.small_signal = (buf[0] == 0xFFFFFFFFu) ? DEF_SMALL_SIGNAL :
                                clamp_f(u32_to_float(buf[0]), SMALL_SIGNAL_MIN, SMALL_SIGNAL_MAX);
        s_params.filter_time  = (buf[1] == 0xFFFFFFFFu) ? DEF_FILTER_TIME :
                                clamp_f(u32_to_float(buf[1]), FILTER_TIME_MIN, FILTER_TIME_MAX);
        s_params.damping_time = (buf[2] == 0xFFFFFFFFu) ? DEF_DAMPING_TIME :
                                clamp_f(u32_to_float(buf[2]), DAMPING_TIME_MIN, DAMPING_TIME_MAX);
    }

    /* 读取输出配置组 (Page 56) */
    {
        uint32_t buf[3];
        ReadBufferFlash(3, PARAM_PAGE_OUTPUT, buf);
        s_params.freq_output  = (buf[0] == 0xFFFFFFFFu) ? DEF_FREQ_OUTPUT :
                                clamp_f(u32_to_float(buf[0]), FREQ_OUTPUT_MIN, FREQ_OUTPUT_MAX);
        s_params.pulse_equiv  = (buf[1] == 0xFFFFFFFFu) ? DEF_PULSE_EQUIV :
                                clamp_u8((uint8_t)buf[1], 0, (uint8_t)(PULSE_EQUIV_COUNT - 1));
        s_params.language     = (buf[2] == 0xFFFFFFFFu) ? DEF_LANGUAGE :
                                clamp_u8((uint8_t)buf[2], 0, (uint8_t)(LANG_COUNT - 1));
    }

    /* 读取介质工况组 (Page 57) */
    {
        uint32_t buf[5];
        ReadBufferFlash(5, PARAM_PAGE_MEDIUM_PARAM, buf);
        s_params.medium_density = (buf[0] == 0xFFFFFFFFu) ? DEF_MEDIUM_DENSITY :
                                  clamp_f(u32_to_float(buf[0]), MEDIUM_DENSITY_MIN, MEDIUM_DENSITY_MAX);
        s_params.pipe_diameter  = (buf[1] == 0xFFFFFFFFu) ? DEF_PIPE_DIAMETER :
                                  clamp_f(u32_to_float(buf[1]), PIPE_DIAMETER_MIN, PIPE_DIAMETER_MAX);
        s_params.gas_ref_press  = (buf[2] == 0xFFFFFFFFu) ? DEF_GAS_REF_PRESS :
                                  clamp_f(u32_to_float(buf[2]), GAS_REF_PRESS_MIN, GAS_REF_PRESS_MAX);
        s_params.gas_ref_temp   = (buf[3] == 0xFFFFFFFFu) ? DEF_GAS_REF_TEMP :
                                  clamp_f(u32_to_float(buf[3]), GAS_REF_TEMP_MIN, GAS_REF_TEMP_MAX);
        s_params.reynolds_k     = (buf[4] == 0xFFFFFFFFu) ? DEF_REYNOLDS_K :
                                  clamp_f(u32_to_float(buf[4]), REYNOLDS_K_MIN, REYNOLDS_K_MAX);
    }

    /* 读取系统/累积组 (Page 58) - 4 字段, 与旧固件兼容
     * 旧固件 baud_rate=0~4 在新格式下自动映射为 uart_config=0~4 (8N1) */
    {
        uint32_t buf[4];
        ReadBufferFlash(4, PARAM_PAGE_SYSTEM, buf);
        s_params.modbus_addr  = (buf[0] == 0xFFFFFFFFu) ? DEF_MODBUS_ADDR :
                                clamp_u16((uint16_t)buf[0], MODBUS_ADDR_MIN, MODBUS_ADDR_MAX);
        if (buf[1] == 0xFFFFFFFFu) {
            s_params.uart_config = DEF_BAUD_RATE; /* 4 = 115200,8N1 */
        } else {
            uint8_t raw = (uint8_t)buf[1];
            uint8_t baud    = uart_cfg_baud(raw);
            uint8_t parity  = uart_cfg_parity(raw);
            uint8_t stop    = uart_cfg_stop(raw);
            if (baud >= BAUD_RATE_COUNT) baud = DEF_BAUD_RATE;
            if (parity >= PARITY_COUNT)  parity = PARITY_NONE;
            if (stop >= STOPBITS_COUNT)  stop = STOPBITS_1;
            s_params.uart_config = uart_cfg_pack(baud, parity, stop);
        }
        s_params.total_factor = (buf[2] == 0xFFFFFFFFu) ? DEF_TOTAL_FACTOR :
                                clamp_f(u32_to_float(buf[2]), TOTAL_FACTOR_MIN, TOTAL_FACTOR_MAX);
        s_params.preset_total = (buf[3] == 0xFFFFFFFFu) ? DEF_PRESET_TOTAL :
                                clamp_f(u32_to_float(buf[3]), PRESET_TOTAL_MIN, PRESET_TOTAL_MAX);
    }

    /* 读取显示/OLED 抗干扰组 (Page 54, Phase 5 新增独立页) */
    {
        uint32_t disp_buf;
        ReadBufferFlash(1, PARAM_PAGE_DISPLAY, &disp_buf);
        s_params.oled_recovery_interval = (disp_buf == 0xFFFFFFFFu) ? DEF_OLED_RECOVERY_INTERVAL :
                                clamp_u16((uint16_t)disp_buf, OLED_RECOVERY_MIN, OLED_RECOVERY_MAX);
    }

    /* value_4ma / value_20ma: 由 main.c 在 Data_Init() 后从 Span 页同步 */
    s_params.value_4ma     = DEF_VALUE_4MA;
    s_params.value_20ma    = DEF_VALUE_20MA;
    /* 累计总量: 频繁变化，不写 Flash，断电后从 0 重算 */
    s_params.forward_total = DEF_FORWARD_TOTAL;
    s_params.reverse_total = DEF_REVERSE_TOTAL;
    s_params.pwd_operator  = DEF_PWD_OPERATOR;
    s_params.pwd_engineer  = DEF_PWD_ENGINEER;

    return HAL_OK;
}

HAL_StatusTypeDef param_storage_get_basic(const param_basic_t **pp_out)
{
    if (!pp_out) return HAL_ERROR;
    *pp_out = &s_params;
    return HAL_OK;
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

/* ===== Phase 1 setter ===== */
HAL_StatusTypeDef param_set_std_cond(uint8_t idx)
{
    uint32_t packed;
    s_params.std_cond = clamp_u8(idx, 0, (uint8_t)(STD_COND_COUNT - 1));
    packed = ((uint32_t)s_params.std_cond << 16) |
             ((uint32_t)s_params.flow_unit << 8) |
             ((uint32_t)s_params.total_unit);
    return (HAL_StatusTypeDef)WriteBufferFlash(1, PARAM_PAGE_BASIC, &packed);
}

HAL_StatusTypeDef param_set_flow_unit(uint8_t idx)
{
    uint32_t packed;
    s_params.flow_unit = clamp_u8(idx, 0, (uint8_t)(FLOW_UNIT_COUNT - 1));
    packed = ((uint32_t)s_params.std_cond << 16) |
             ((uint32_t)s_params.flow_unit << 8) |
             ((uint32_t)s_params.total_unit);
    return (HAL_StatusTypeDef)WriteBufferFlash(1, PARAM_PAGE_BASIC, &packed);
}

HAL_StatusTypeDef param_set_total_unit(uint8_t idx)
{
    uint32_t packed;
    s_params.total_unit = clamp_u8(idx, 0, (uint8_t)(TOTAL_UNIT_COUNT - 1));
    packed = ((uint32_t)s_params.std_cond << 16) |
             ((uint32_t)s_params.flow_unit << 8) |
             ((uint32_t)s_params.total_unit);
    return (HAL_StatusTypeDef)WriteBufferFlash(1, PARAM_PAGE_BASIC, &packed);
}

HAL_StatusTypeDef param_set_meter_coeff(float val)
{
    s_params.meter_coeff = clamp_f(val, METER_COEFF_MIN, METER_COEFF_MAX);
    return flush_meter_cal_group();
}

HAL_StatusTypeDef param_set_medium_coeff(float val)
{
    uint32_t buf;
    s_params.medium_coeff = clamp_f(val, MEDIUM_COEFF_MIN, MEDIUM_COEFF_MAX);
    buf = float_to_u32(s_params.medium_coeff);
    return (HAL_StatusTypeDef)WriteBufferFlash(1, PARAM_PAGE_MEDIUM, &buf);
}

HAL_StatusTypeDef param_set_small_signal(float val)
{
    s_params.small_signal = clamp_f(val, SMALL_SIGNAL_MIN, SMALL_SIGNAL_MAX);
    return flush_signal_group();
}

HAL_StatusTypeDef param_set_filter_time(float val)
{
    s_params.filter_time = clamp_f(val, FILTER_TIME_MIN, FILTER_TIME_MAX);
    return flush_signal_group();
}

HAL_StatusTypeDef param_set_damping_time(float val)
{
    s_params.damping_time = clamp_f(val, DAMPING_TIME_MIN, DAMPING_TIME_MAX);
    return flush_signal_group();
}

/* ===== Phase 2 输出 setter — RAM only, Flash 由 FC10 BackupBuf 路径持久化 ===== */
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

HAL_StatusTypeDef param_set_freq_output(float val)
{
    s_params.freq_output = clamp_f(val, FREQ_OUTPUT_MIN, FREQ_OUTPUT_MAX);
    return flush_output_group();
}

HAL_StatusTypeDef param_set_pulse_equiv(uint8_t idx)
{
    s_params.pulse_equiv = clamp_u8(idx, 0, (uint8_t)(PULSE_EQUIV_COUNT - 1));
    return flush_output_group();
}

/* ===== Phase 2 介质/工况 setter ===== */
HAL_StatusTypeDef param_set_medium_density(float val)
{
    s_params.medium_density = clamp_f(val, MEDIUM_DENSITY_MIN, MEDIUM_DENSITY_MAX);
    return flush_medium_param_group();
}

HAL_StatusTypeDef param_set_pipe_diameter(float val)
{
    s_params.pipe_diameter = clamp_f(val, PIPE_DIAMETER_MIN, PIPE_DIAMETER_MAX);
    return flush_medium_param_group();
}

HAL_StatusTypeDef param_set_gas_ref_press(float val)
{
    s_params.gas_ref_press = clamp_f(val, GAS_REF_PRESS_MIN, GAS_REF_PRESS_MAX);
    return flush_medium_param_group();
}

HAL_StatusTypeDef param_set_gas_ref_temp(float val)
{
    s_params.gas_ref_temp = clamp_f(val, GAS_REF_TEMP_MIN, GAS_REF_TEMP_MAX);
    return flush_medium_param_group();
}

HAL_StatusTypeDef param_set_reynolds_k(float val)
{
    s_params.reynolds_k = clamp_f(val, REYNOLDS_K_MIN, REYNOLDS_K_MAX);
    return flush_medium_param_group();
}

/* ===== Phase 2 累积器 setter ===== */
HAL_StatusTypeDef param_set_total_factor(float val)
{
    s_params.total_factor = clamp_f(val, TOTAL_FACTOR_MIN, TOTAL_FACTOR_MAX);
    return flush_system_group();
}

HAL_StatusTypeDef param_set_preset_total(float val)
{
    s_params.preset_total = clamp_f(val, PRESET_TOTAL_MIN, PRESET_TOTAL_MAX);
    return flush_system_group();
}

/* ===== Phase 3 累计总量 setter (频繁变化，不持久化，断电后重置) ===== */
HAL_StatusTypeDef param_set_forward_total(float val)
{
    s_params.forward_total = clamp_f(val, FORWARD_TOTAL_MIN, FORWARD_TOTAL_MAX);
    return HAL_OK;
}

HAL_StatusTypeDef param_set_reverse_total(float val)
{
    s_params.reverse_total = clamp_f(val, REVERSE_TOTAL_MIN, REVERSE_TOTAL_MAX);
    return HAL_OK;
}

/* ===== Phase 4 系统 setter ===== */
HAL_StatusTypeDef param_set_modbus_addr(uint16_t addr)
{
    s_params.modbus_addr = clamp_u16(addr, MODBUS_ADDR_MIN, MODBUS_ADDR_MAX);
    return flush_system_group();
}

HAL_StatusTypeDef param_set_baud_rate(uint8_t idx)
{
    uint8_t cfg = s_params.uart_config;
    s_params.uart_config = (cfg & 0x38u) | (uint8_t)clamp_u8(idx, 0, (uint8_t)(BAUD_RATE_COUNT - 1));
    return flush_system_group();
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
    s_params.uart_config = cfg;
    return flush_system_group();
}

HAL_StatusTypeDef param_set_language(uint8_t idx)
{
    s_params.language = clamp_u8(idx, 0, (uint8_t)(LANG_COUNT - 1));
    return flush_output_group();
}

/* ===== Phase 5 OLED 自愈 setter ===== */
HAL_StatusTypeDef param_set_oled_recovery_interval(uint16_t val)
{
    s_params.oled_recovery_interval = clamp_u16(val, OLED_RECOVERY_MIN, OLED_RECOVERY_MAX);
    return flush_display_group();
}

/* ===== Phase 6 标定 getter ===== */
uint8_t  param_get_cal_enabled(void)          { return s_params.cal_enabled; }
float    param_get_cal_k(uint8_t index)       { return (index < CAL_POINT_COUNT) ? s_params.cal_k[index] : 1.0f; }
float    param_get_cal_pct(uint8_t index)     { return (index < CAL_POINT_COUNT) ? s_params.cal_pct[index] : 0.0f; }

/* ===== Phase 6 标定 setter ===== */
HAL_StatusTypeDef param_set_cal_enabled(uint8_t val)
{
    s_params.cal_enabled = (val) ? 1 : 0;
    return flush_meter_cal_group();
}

HAL_StatusTypeDef param_set_cal_k(uint8_t index, float val)
{
    if (index >= CAL_POINT_COUNT) return HAL_ERROR;
    s_params.cal_k[index] = clamp_f(val, CAL_K_MIN, CAL_K_MAX);
    return flush_meter_cal_group();
}

HAL_StatusTypeDef param_set_cal_pct(uint8_t index, float val)
{
    if (index >= CAL_POINT_COUNT) return HAL_ERROR;
    s_params.cal_pct[index] = clamp_f(val, CAL_PCT_MIN, CAL_PCT_MAX);
    return flush_meter_cal_group();
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
    /* Phase 1 */
    param_set_std_cond(DEF_STD_COND);
    param_set_meter_coeff(DEF_METER_COEFF);
    param_set_medium_coeff(DEF_MEDIUM_COEFF);
    param_set_flow_unit(DEF_FLOW_UNIT);
    param_set_total_unit(DEF_TOTAL_UNIT);
    param_set_small_signal(DEF_SMALL_SIGNAL);
    param_set_filter_time(DEF_FILTER_TIME);
    param_set_damping_time(DEF_DAMPING_TIME);
    /* Phase 2 输出 */
    param_set_value_4ma(DEF_VALUE_4MA);
    param_set_value_20ma(DEF_VALUE_20MA);
    param_set_freq_output(DEF_FREQ_OUTPUT);
    param_set_pulse_equiv(DEF_PULSE_EQUIV);
    /* Phase 2 介质/工况 */
    param_set_medium_density(DEF_MEDIUM_DENSITY);
    param_set_pipe_diameter(DEF_PIPE_DIAMETER);
    param_set_gas_ref_press(DEF_GAS_REF_PRESS);
    param_set_gas_ref_temp(DEF_GAS_REF_TEMP);
    param_set_reynolds_k(DEF_REYNOLDS_K);
    /* Phase 2 累积器 */
    param_set_total_factor(DEF_TOTAL_FACTOR);
    param_set_preset_total(DEF_PRESET_TOTAL);
    /* Phase 3 累计总量 */
    param_set_forward_total(DEF_FORWARD_TOTAL);
    param_set_reverse_total(DEF_REVERSE_TOTAL);
    /* Phase 4 系统 */
    param_set_modbus_addr(DEF_MODBUS_ADDR);
    param_set_baud_rate(DEF_BAUD_RATE);
    param_set_language(DEF_LANGUAGE);
    /* Phase 5 OLED 自愈 */
    param_set_oled_recovery_interval(DEF_OLED_RECOVERY_INTERVAL);
    /* Phase 6 标定 */
    {
        int i;
        s_params.cal_enabled = DEF_CAL_ENABLED;
        for (i = 0; i < CAL_POINT_COUNT; i++) s_params.cal_k[i] = DEF_CAL_K;
        s_params.cal_pct[0] = DEF_CAL_PCT_0;
        s_params.cal_pct[1] = DEF_CAL_PCT_1;
        s_params.cal_pct[2] = DEF_CAL_PCT_2;
        s_params.cal_pct[3] = DEF_CAL_PCT_3;
        s_params.cal_pct[4] = DEF_CAL_PCT_4;
        s_params.cal_pct[5] = DEF_CAL_PCT_5;
        s_params.cal_pct[6] = DEF_CAL_PCT_6;
    }
    /* 密码 (仅 RAM，不持久化) */
    s_params.pwd_operator = DEF_PWD_OPERATOR;
    s_params.pwd_engineer = DEF_PWD_ENGINEER;
    /* 将所有分组同步写入 Flash */
    flush_signal_group();
    flush_output_group();
    flush_medium_param_group();
    flush_system_group();
    flush_meter_cal_group();
    return HAL_OK;
}
