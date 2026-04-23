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
#define DEF_BAUD_RATE      3
#define DEF_PWD_OPERATOR   0
#define DEF_PWD_ENGINEER   123

/* ===== 范围限制 ===== */
#define METER_COEFF_MIN    0.001f
#define METER_COEFF_MAX    99.999f
#define MEDIUM_COEFF_MIN   0.100f
#define MEDIUM_COEFF_MAX   10.000f
#define SMALL_SIGNAL_MIN   0.5f
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

/* Flash 页分配 (不与现有 Page 63 冲突) */
#define PARAM_PAGE_BASIC   ADDR_FLASH_PAGE_60  /* std_cond + flow_unit + total_unit */
#define PARAM_PAGE_METER   ADDR_FLASH_PAGE_61  /* meter_coeff */
#define PARAM_PAGE_MEDIUM  ADDR_FLASH_PAGE_62  /* medium_coeff */

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
    "4800", "9600", "19200", "38400", "115200"
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

    /* 读取 Page 61: meter_coeff */
    ReadBufferFlash(1, PARAM_PAGE_METER, &buf);
    s_params.meter_coeff = (buf == 0xFFFFFFFF) ? DEF_METER_COEFF : u32_to_float(buf);
    s_params.meter_coeff = clamp_f(s_params.meter_coeff, METER_COEFF_MIN, METER_COEFF_MAX);

    /* 读取 Page 62: medium_coeff */
    ReadBufferFlash(1, PARAM_PAGE_MEDIUM, &buf);
    s_params.medium_coeff = (buf == 0xFFFFFFFF) ? DEF_MEDIUM_COEFF : u32_to_float(buf);
    s_params.medium_coeff = clamp_f(s_params.medium_coeff, MEDIUM_COEFF_MIN, MEDIUM_COEFF_MAX);

    /* Phase 1: 暂用默认值 */
    s_params.small_signal  = DEF_SMALL_SIGNAL;
    s_params.filter_time   = DEF_FILTER_TIME;
    s_params.damping_time  = DEF_DAMPING_TIME;

    /* Phase 2+: 暂用默认值, TODO: 后续从 Flash 读取 */
    /* value_4ma / value_20ma: 使用默认值, 由 main.c 在 Data_Init() 后同步 */
    s_params.value_4ma       = DEF_VALUE_4MA;
    s_params.value_20ma      = DEF_VALUE_20MA;
    s_params.freq_output     = DEF_FREQ_OUTPUT;
    s_params.pulse_equiv     = DEF_PULSE_EQUIV;
    s_params.medium_density  = DEF_MEDIUM_DENSITY;
    s_params.pipe_diameter   = DEF_PIPE_DIAMETER;
    s_params.gas_ref_press   = DEF_GAS_REF_PRESS;
    s_params.gas_ref_temp    = DEF_GAS_REF_TEMP;
    s_params.reynolds_k      = DEF_REYNOLDS_K;
    s_params.total_factor    = DEF_TOTAL_FACTOR;
    s_params.preset_total    = DEF_PRESET_TOTAL;
    s_params.forward_total   = DEF_FORWARD_TOTAL;
    s_params.reverse_total   = DEF_REVERSE_TOTAL;
    s_params.modbus_addr     = DEF_MODBUS_ADDR;
    s_params.baud_rate       = DEF_BAUD_RATE;
    s_params.pwd_operator    = DEF_PWD_OPERATOR;
    s_params.pwd_engineer    = DEF_PWD_ENGINEER;

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
uint8_t  param_get_baud_rate(void)   { return s_params.baud_rate; }
uint16_t param_get_pwd_engineer(void) { return s_params.pwd_engineer; }

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
    uint32_t buf;
    s_params.meter_coeff = clamp_f(val, METER_COEFF_MIN, METER_COEFF_MAX);
    buf = float_to_u32(s_params.meter_coeff);
    return (HAL_StatusTypeDef)WriteBufferFlash(1, PARAM_PAGE_METER, &buf);
}

HAL_StatusTypeDef param_set_medium_coeff(float val)
{
    uint32_t buf;
    s_params.medium_coeff = clamp_f(val, MEDIUM_COEFF_MIN, MEDIUM_COEFF_MAX);
    buf = float_to_u32(s_params.medium_coeff);
    return (HAL_StatusTypeDef)WriteBufferFlash(1, PARAM_PAGE_MEDIUM, &buf);
}

/* small_signal / filter_time / damping_time: 暂只更新 RAM, TODO 写 Flash */
HAL_StatusTypeDef param_set_small_signal(float val)
{
    s_params.small_signal = clamp_f(val, SMALL_SIGNAL_MIN, SMALL_SIGNAL_MAX);
    return HAL_OK;
}

HAL_StatusTypeDef param_set_filter_time(float val)
{
    s_params.filter_time = clamp_f(val, FILTER_TIME_MIN, FILTER_TIME_MAX);
    return HAL_OK;
}

HAL_StatusTypeDef param_set_damping_time(float val)
{
    s_params.damping_time = clamp_f(val, DAMPING_TIME_MIN, DAMPING_TIME_MAX);
    return HAL_OK;
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
    return HAL_OK;
}

HAL_StatusTypeDef param_set_pulse_equiv(uint8_t idx)
{
    s_params.pulse_equiv = clamp_u8(idx, 0, (uint8_t)(PULSE_EQUIV_COUNT - 1));
    return HAL_OK;
}

/* ===== Phase 2 介质/工况 setter (TODO: 写 Flash) ===== */
HAL_StatusTypeDef param_set_medium_density(float val)
{
    s_params.medium_density = clamp_f(val, MEDIUM_DENSITY_MIN, MEDIUM_DENSITY_MAX);
    return HAL_OK;
}

HAL_StatusTypeDef param_set_pipe_diameter(float val)
{
    s_params.pipe_diameter = clamp_f(val, PIPE_DIAMETER_MIN, PIPE_DIAMETER_MAX);
    return HAL_OK;
}

HAL_StatusTypeDef param_set_gas_ref_press(float val)
{
    s_params.gas_ref_press = clamp_f(val, GAS_REF_PRESS_MIN, GAS_REF_PRESS_MAX);
    return HAL_OK;
}

HAL_StatusTypeDef param_set_gas_ref_temp(float val)
{
    s_params.gas_ref_temp = clamp_f(val, GAS_REF_TEMP_MIN, GAS_REF_TEMP_MAX);
    return HAL_OK;
}

HAL_StatusTypeDef param_set_reynolds_k(float val)
{
    s_params.reynolds_k = clamp_f(val, REYNOLDS_K_MIN, REYNOLDS_K_MAX);
    return HAL_OK;
}

/* ===== Phase 2 累积器 setter (TODO: 写 Flash) ===== */
HAL_StatusTypeDef param_set_total_factor(float val)
{
    s_params.total_factor = clamp_f(val, TOTAL_FACTOR_MIN, TOTAL_FACTOR_MAX);
    return HAL_OK;
}

HAL_StatusTypeDef param_set_preset_total(float val)
{
    s_params.preset_total = clamp_f(val, PRESET_TOTAL_MIN, PRESET_TOTAL_MAX);
    return HAL_OK;
}

/* ===== Phase 3 累计总量 setter (TODO: 写 Flash) ===== */
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

/* ===== Phase 4 系统 setter (TODO: 写 Flash) ===== */
HAL_StatusTypeDef param_set_modbus_addr(uint16_t addr)
{
    s_params.modbus_addr = clamp_u16(addr, MODBUS_ADDR_MIN, MODBUS_ADDR_MAX);
    return HAL_OK;
}

HAL_StatusTypeDef param_set_baud_rate(uint8_t idx)
{
    s_params.baud_rate = clamp_u8(idx, 0, (uint8_t)(BAUD_RATE_COUNT - 1));
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
    /* 密码 */
    s_params.pwd_operator = DEF_PWD_OPERATOR;
    s_params.pwd_engineer = DEF_PWD_ENGINEER;
    return HAL_OK;
}
