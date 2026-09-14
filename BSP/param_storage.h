/**
 * @file    param_storage.h
 * @brief   参数存储模块 — RAM 缓存 + Flash 持久化
 *
 * 菜单模块是唯一的 setter 调用者。
 * 其他模块通过 getter 读取参数。
 */
#ifndef __PARAM_STORAGE_H
#define __PARAM_STORAGE_H

#include "stm32f1xx_hal.h"

/* ===== Phase 1: 基本设置枚举 ===== */

/* 标况选项枚举 */
typedef enum {
    STD_COND_0C  = 0,  /* 101.3KPa / 0℃ */
    STD_COND_20C = 1,  /* 101.3KPa / 20℃ */
    STD_COND_25C = 2,  /* 101.3KPa / 25℃ */
    STD_COND_COUNT
} std_cond_t;

/* 流量单位枚举 */
typedef enum {
    FLOW_UNIT_M3H  = 0,  /* m3/h */
    FLOW_UNIT_LH   = 1,  /* L/h */
    FLOW_UNIT_LMIN = 2,  /* L/min */
    FLOW_UNIT_KGH  = 3,  /* kg/h */
    FLOW_UNIT_COUNT
} flow_unit_t;

/* 累积单位枚举 */
typedef enum {
    TOTAL_UNIT_M3 = 0,  /* m3 */
    TOTAL_UNIT_L  = 1,  /* L */
    TOTAL_UNIT_KG = 2,  /* kg */
    TOTAL_UNIT_T  = 3,  /* t */
    TOTAL_UNIT_COUNT
} total_unit_t;

/* ===== Phase 2: 输出设置枚举 ===== */

/* 脉冲当量 */
typedef enum {
    PULSE_0_001 = 0, PULSE_0_01 = 1, PULSE_0_1 = 2,
    PULSE_1 = 3, PULSE_10 = 4, PULSE_100 = 5,
    PULSE_EQUIV_COUNT
} pulse_equiv_t;

/* 波特率 */
typedef enum {
    BAUD_4800 = 0, BAUD_9600 = 1, BAUD_19200 = 2,
    BAUD_38400 = 3, BAUD_115200 = 4, BAUD_2400 = 5,
    BAUD_RATE_COUNT
} baud_rate_t;

/* 校验位 */
typedef enum {
    PARITY_NONE = 0,
    PARITY_ODD  = 1,
    PARITY_EVEN = 2,
    PARITY_COUNT
} parity_t;

/* 停止位 */
typedef enum {
    STOPBITS_1 = 0,
    STOPBITS_2 = 1,
    STOPBITS_COUNT
} stopbits_t;

/* uart_config 位域编解码
 *   bit 2:0 = 波特率索引 (baud_rate_t)
 *   bit 4:3 = 校验位 (parity_t)
 *   bit 5   = 停止位 (stopbits_t)
 *   bit 7:6 = 保留 (0) */
static inline uint8_t uart_cfg_baud(uint8_t cfg)   { return cfg & 0x07u; }
static inline uint8_t uart_cfg_parity(uint8_t cfg) { return (cfg >> 3) & 0x03u; }
static inline uint8_t uart_cfg_stop(uint8_t cfg)   { return (cfg >> 5) & 0x01u; }
static inline uint8_t uart_cfg_pack(uint8_t baud, uint8_t par, uint8_t stop)
    { return (uint8_t)((stop << 5) | (par << 3) | baud); }

/* 语言 (仅英文, 中文已移除以释放 Flash; 保留枚举以兼容菜单 SCR_LANGUAGE 屏幕) */
typedef enum {
    LANG_ENGLISH = 0,
    LANG_COUNT
} language_t;

/* ===== 参数集合结构体 ===== */

typedef struct {
    /* --- Phase 1: 基本设置 --- */
    uint8_t  std_cond;       /* 标况索引 (std_cond_t) */
    float    meter_coeff;    /* 仪表系数 0.001~99.999 */
    float    medium_coeff;   /* 介质系数 0.100~10.000, 默认 1.0 */
    uint8_t  flow_unit;      /* 流量单位索引 (flow_unit_t) */
    uint8_t  total_unit;     /* 累积单位索引 (total_unit_t) */
    float    small_signal;   /* 小信号切除 0.0~10.0 (%) */
    float    filter_time;    /* 滤波参数 0.1~100.0 (秒) */
    float    damping_time;   /* 阻尼时间 0.1~100.0 (秒) */

    /* --- Phase 2: 输出设置 --- */
    float    value_4ma;      /* -9999.0~99999.0, 默认 0.0 */
    float    value_20ma;     /* 0.1~99999.0, 默认 100.0 */
    float    freq_output;    /* 0~10000 Hz, 默认 1000.0 */
    uint8_t  pulse_equiv;    /* 0~5, 默认 0 */

    /* --- Phase 2: 介质/工况 --- */
    float    medium_density; /* 0.001~99999.0 kg/m3, 默认 1000.0 */
    float    pipe_diameter;  /* 0.1~99999.0 mm, 默认 25.0 */
    float    gas_ref_press;  /* 0.0~99999.0 KPa, 默认 101.3 */
    float    gas_ref_temp;   /* -40.0~200.0 C, 默认 20.0 */
    float    reynolds_k;     /* 0.001~10.000, 默认 1.000 */

    /* --- Phase 2: 累积器 --- */
    float    total_factor;   /* 0.001~99.999, 默认 1.000 */
    float    preset_total;   /* 0.0~9999999.0, 默认 0.0 */

    /* --- Phase 3: 累计总量 --- */
    float    forward_total;  /* 0.0~9999999.0, 默认 0.0 */
    float    reverse_total;  /* 0.0~9999999.0, 默认 0.0 */

    /* --- Phase 4: 系统 --- */
    uint16_t modbus_addr;    /* 1~247, 默认 2 */
    uint8_t  uart_config;    /* packed: [5]=stop [4:3]=parity [2:0]=baud, 默认 4 (115200,8N1) */
    uint8_t  language;       /* language_t, 默认 0 (English) */

    /* --- Phase 5: OLED 抗干扰自愈 --- */
    uint16_t oled_recovery_interval; /* 重初始化间隔 (单位: 100ms)
                                      *   0     = 禁用周期性自愈
                                      *   50    = 5s (默认)
                                      *   1~600 = 0.1~60 秒
                                      * 上位机可通过 Modbus 寄存器 40095 读写 */

    /* --- Phase 6: 七点流量标定 --- */
    uint8_t  cal_enabled;     /* 标定使能, 默认 0 */
    float    cal_k[7];        /* 标定修正系数, 默认 1.0 */
    float    cal_pct[7];      /* 标定点百分比, 默认 [0,3,10,25,50,75,100] */

    /* --- 密码 --- */
    uint16_t pwd_operator;   /* 默认 0 */
    uint16_t pwd_engineer;   /* 开发者/工程师密码，默认 123 */
} param_basic_t;

/* ===== 初始化/批量读取 ===== */

/* 初始化: 从 Flash 加载全部参数到 RAM 缓存 */
HAL_StatusTypeDef param_storage_init(void);

/* 批量读取 (只读快照) */
HAL_StatusTypeDef param_storage_get_basic(const param_basic_t **pp_out);

/* ===== Phase 1 getter ===== */
uint8_t  param_get_std_cond(void);
float    param_get_meter_coeff(void);
float    param_get_medium_coeff(void);
uint8_t  param_get_flow_unit(void);
uint8_t  param_get_total_unit(void);
float    param_get_small_signal(void);
float    param_get_filter_time(void);
float    param_get_damping_time(void);
uint16_t param_get_pwd_operator(void);

/* ===== Phase 2 输出 getter ===== */
float    param_get_value_4ma(void);
float    param_get_value_20ma(void);
float    param_get_freq_output(void);
uint8_t  param_get_pulse_equiv(void);

/* ===== Phase 2 介质/工况 getter ===== */
float    param_get_medium_density(void);
float    param_get_pipe_diameter(void);
float    param_get_gas_ref_press(void);
float    param_get_gas_ref_temp(void);
float    param_get_reynolds_k(void);

/* ===== Phase 2 累积器 getter ===== */
float    param_get_total_factor(void);
float    param_get_preset_total(void);

/* ===== Phase 3 累计总量 getter ===== */
float    param_get_forward_total(void);
float    param_get_reverse_total(void);

/* ===== Phase 4 系统 getter ===== */
uint16_t param_get_modbus_addr(void);
uint8_t  param_get_baud_rate(void);     /* 返回波特率索引 (uart_config bit[2:0]) */
uint8_t  param_get_uart_config(void);   /* 返回完整 packed uart_config */
uint16_t param_get_pwd_engineer(void);
uint8_t  param_get_language(void);

/* ===== Phase 5 OLED 自愈 getter ===== */
uint16_t param_get_oled_recovery_interval(void);

/* ===== Phase 1 setter ===== */
HAL_StatusTypeDef param_set_std_cond(uint8_t idx);
HAL_StatusTypeDef param_set_meter_coeff(float val);
HAL_StatusTypeDef param_set_medium_coeff(float val);
HAL_StatusTypeDef param_set_flow_unit(uint8_t idx);
HAL_StatusTypeDef param_set_total_unit(uint8_t idx);
HAL_StatusTypeDef param_set_small_signal(float val);
HAL_StatusTypeDef param_set_filter_time(float val);
HAL_StatusTypeDef param_set_damping_time(float val);

/* ===== Phase 2 输出 setter ===== */
HAL_StatusTypeDef param_set_value_4ma(float val);
HAL_StatusTypeDef param_set_value_20ma(float val);
HAL_StatusTypeDef param_set_freq_output(float val);
HAL_StatusTypeDef param_set_pulse_equiv(uint8_t idx);

/* ===== Phase 2 介质/工况 setter ===== */
HAL_StatusTypeDef param_set_medium_density(float val);
HAL_StatusTypeDef param_set_pipe_diameter(float val);
HAL_StatusTypeDef param_set_gas_ref_press(float val);
HAL_StatusTypeDef param_set_gas_ref_temp(float val);
HAL_StatusTypeDef param_set_reynolds_k(float val);

/* ===== Phase 2 累积器 setter ===== */
HAL_StatusTypeDef param_set_total_factor(float val);
HAL_StatusTypeDef param_set_preset_total(float val);

/* ===== Phase 3 累计总量 setter ===== */
HAL_StatusTypeDef param_set_forward_total(float val);
HAL_StatusTypeDef param_set_reverse_total(float val);

/* ===== Phase 4 系统 setter ===== */
HAL_StatusTypeDef param_set_modbus_addr(uint16_t addr);
HAL_StatusTypeDef param_set_baud_rate(uint8_t idx);      /* 仅修改波特率索引, 保留校验/停止位 */
HAL_StatusTypeDef param_set_uart_config(uint8_t cfg);    /* 设置完整 packed uart_config (Modbus 用) */
HAL_StatusTypeDef param_set_language(uint8_t idx);

/* ===== Phase 5 OLED 自愈 setter ===== */
HAL_StatusTypeDef param_set_oled_recovery_interval(uint16_t val);

/* ===== Phase 6 标定 getter ===== */
uint8_t  param_get_cal_enabled(void);
float    param_get_cal_k(uint8_t index);      /* index 0~6 */
float    param_get_cal_pct(uint8_t index);    /* index 0~6 */

/* ===== Phase 6 标定 setter ===== */
HAL_StatusTypeDef param_set_cal_enabled(uint8_t val);
HAL_StatusTypeDef param_set_cal_k(uint8_t index, float val);
HAL_StatusTypeDef param_set_cal_pct(uint8_t index, float val);

/* ===== 枚举字符串 (菜单渲染用) ===== */
const char *param_get_std_cond_str(uint8_t idx);
const char *param_get_flow_unit_str(uint8_t idx);
const char *param_get_total_unit_str(uint8_t idx);

const char * const *param_get_std_cond_strings(void);
const char * const *param_get_flow_unit_strings(void);
const char * const *param_get_total_unit_strings(void);
const char * const *param_get_pulse_equiv_strings(void);
const char * const *param_get_baud_rate_strings(void);

/* 恢复出厂默认值 */
HAL_StatusTypeDef param_storage_reset_defaults(void);

#endif /* __PARAM_STORAGE_H */
