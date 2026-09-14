/**
 * @file    bsp_menu.c
 * @brief   菜单系统实现 — 导航栈 + 6模式 + 密码门控
 * @note    对齐 UMF_HMI_Screen_Design.md v1.0, 参照 coriolis_drive bsp_menu 架构
 *
 * 模式:
 *   MODE_LIST     (M1): 滚动列表菜单
 *   MODE_NUMERIC  (M2): 数值编辑
 *   MODE_ENUM     (M3): 枚举选择
 *   MODE_PASSWORD (M6): 密码输入
 *   MODE_READONLY (M4): 只读显示
 *   MODE_CONFIRM  (M5): 确认对话框
 */
#include "bsp_menu.h"
#include "param_storage.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "main.h"
#include "ftoa.h"
#include "mystring.h"
#include <string.h>

/* ===== 模式枚举 ===== */
typedef enum {
    MODE_LIST     = 0,
    MODE_NUMERIC  = 1,
    MODE_ENUM     = 2,
    MODE_PASSWORD = 3,
    MODE_READONLY = 4,
    MODE_CONFIRM  = 5
} menu_mode_t;

/* ===== 屏幕 ID (对齐 UMF_HMI_Screen_Design.md) ===== */
typedef enum {
    SCR_MAIN_MENU    = 3,
    SCR_PASSWORD     = 4,
    SCR_BASIC_LIST   = 5,
    SCR_STD_COND     = 6,
    SCR_METER_COEFF  = 7,
    SCR_MEDIUM_COEFF = 8,
    SCR_FLOW_UNIT    = 9,
    SCR_TOTAL_UNIT   = 10,
    SCR_SMALL_SIGNAL = 11,
    SCR_FILTER_TIME  = 12,
    SCR_DAMPING_TIME = 13,
    SCR_OUTPUT_LIST  = 14,
    SCR_4MA_VALUE    = 15,
    SCR_20MA_VALUE   = 16,
    SCR_FREQ_OUTPUT  = 17,
    SCR_PULSE_EQUIV  = 18,
    SCR_MEDIUM_LIST  = 19,
    SCR_DENSITY      = 20,
    SCR_PIPE_DIA     = 21,
    SCR_GAS_PRESS    = 22,
    SCR_GAS_TEMP     = 23,
    SCR_REYNOLDS     = 24,
    SCR_TOTALIZER_LIST = 25,
    SCR_TOTAL_FACTOR = 26,
    SCR_PRESET_TOTAL = 27,
    SCR_ACCUM_LIST   = 28,
    SCR_FWD_TOTAL    = 29,
    SCR_REV_TOTAL    = 30,
    SCR_NET_TOTAL    = 31,
    SCR_CLEAR_TOTALS = 32,
    SCR_SET_TOTAL    = 33,
    SCR_CALIB_LIST   = 34,
    SCR_DAC_ZERO     = 35,
    SCR_DAC_FULL     = 36,
    SCR_SPAN_ZERO    = 37,
    SCR_SPAN_FULL    = 38,
    SCR_SYSTEM_LIST  = 39,
    SCR_FACTORY_RST  = 40,
    SCR_COMM_ADDR    = 41,
    SCR_BAUD_RATE    = 42,
    SCR_DEVICE_INFO  = 43,
    SCR_LANGUAGE     = 44,
    SCR_COUNT        = 45
} screen_t;

/* ===== 导航栈帧 ===== */
#define NAV_STACK_DEPTH  5
#define COEFF_DIGIT_COUNT 5
#define PASSWORD_ERROR_DISPLAY_MS 2000U
#define OPERATOR_COEFF_MIN 0.800f
#define OPERATOR_COEFF_MAX 1.200f

typedef enum {
    ACCESS_NONE = 0,
    ACCESS_OPERATOR,
    ACCESS_DEVELOPER
} access_level_t;

typedef struct {
    menu_mode_t mode;
    screen_t    screen_id;
    uint8_t     cursor;         /* LIST/ENUM: 选中项; PASSWORD: 编辑位 */
    uint8_t     scroll;         /* LIST: 滚动窗口起始 */
    float       edit_val;       /* NUMERIC: 临时编辑值 */
    uint8_t     coeff_digits[COEFF_DIGIT_COUNT]; /* NUMERIC: Coeff 的 00.000 数位 */
    uint8_t     pwd_digits[3];  /* PASSWORD: 3位数字 */
    uint8_t     pwd_target;     /* PASSWORD: 成功后跳转的目标屏幕 */
    uint32_t    pwd_err_start_ms;     /* PASSWORD: 错误提示开始时刻 */
    uint8_t     pwd_err_visible;      /* PASSWORD: 错误提示正在显示 */
    uint8_t     confirm_sel;    /* CONFIRM: 0=NO(安全默认), 1=YES */
    uint8_t     enum_val;       /* ENUM: 临时枚举值 */
} nav_frame_t;

/* ===== 列表项定义 ===== */
typedef struct {
    const char *label;
    uint8_t     target;  /* screen_id */
} list_item_t;

/* S03 主菜单 (5 项) */
static const list_item_t c_main_menu[] = {
    { "1.Display",    0               },
    { "2.Parameter",  SCR_BASIC_LIST  },
    { "3.Totalizer",  SCR_ACCUM_LIST  },
    { "4.Calibration", SCR_CALIB_LIST },
    { "5.System",     SCR_SYSTEM_LIST },
};
#define MAIN_MENU_COUNT  5

/* S05 基本设置子菜单 (11 项) */
static const list_item_t c_basic_set[] = {
    { "Std.Cond",      SCR_STD_COND     },
    { "Meter Coeff",   SCR_METER_COEFF  },
    { "Medium Coeff",  SCR_MEDIUM_COEFF },
    { "Flow Unit",     SCR_FLOW_UNIT    },
    { "Total Unit",    SCR_TOTAL_UNIT   },
    { "Small Sig",     SCR_SMALL_SIGNAL },
    { "Filter Parm",   SCR_FILTER_TIME  },
    { "Damping Time",  SCR_DAMPING_TIME },
    { "Output Set",    SCR_OUTPUT_LIST  },
    { "Medium/Cond",   SCR_MEDIUM_LIST  },
    { "Totalizer Set", SCR_TOTALIZER_LIST },
};
#define BASIC_SET_COUNT  11

/* S14 输出设置 (4 项) */
static const list_item_t c_output_set[] = {
    { "4mA Value",    SCR_4MA_VALUE   },
    { "20mA Value",   SCR_20MA_VALUE  },
    { "Freq Output",  SCR_FREQ_OUTPUT },
    { "Pulse Equiv",  SCR_PULSE_EQUIV },
};
#define OUTPUT_SET_COUNT  4

/* S19 介质/工况 (5 项) */
static const list_item_t c_medium_set[] = {
    { "Density",     SCR_DENSITY   },
    { "Pipe Dia",    SCR_PIPE_DIA  },
    { "Gas Press",   SCR_GAS_PRESS },
    { "Gas Temp",    SCR_GAS_TEMP  },
    { "Reynolds K",  SCR_REYNOLDS  },
};
#define MEDIUM_SET_COUNT  5

/* S25 累积器设置 (3 项, Total Unit 复用 S10) */
static const list_item_t c_totalizer_set[] = {
    { "Total Unit",   SCR_TOTAL_UNIT   },
    { "Total Factor", SCR_TOTAL_FACTOR },
    { "Preset Total", SCR_PRESET_TOTAL },
};
#define TOTALIZER_SET_COUNT  3

/* S28 累计总量管理 (5 项) */
static const list_item_t c_accum_set[] = {
    { "Fwd Total",     SCR_FWD_TOTAL    },
    { "Rev Total",     SCR_REV_TOTAL    },
    { "Net Total",     SCR_NET_TOTAL    },
    { "Clear All",     SCR_CLEAR_TOTALS },
    { "Set Total",     SCR_SET_TOTAL    },
};
#define ACCUM_SET_COUNT  5

/* S34 校准 (4 项) */
static const list_item_t c_calib_set[] = {
    { "DAC Zero",     SCR_DAC_ZERO  },
    { "DAC Full",     SCR_DAC_FULL  },
    { "Span Zero",    SCR_SPAN_ZERO },
    { "Span Full",    SCR_SPAN_FULL },
};
#define CALIB_SET_COUNT  4

/* S39 系统设置 (5 项) */
static const list_item_t c_system_set[] = {
    { "Language",     SCR_LANGUAGE    },
    { "Factory Rst",  SCR_FACTORY_RST },
    { "Comm Addr",    SCR_COMM_ADDR   },
    { "Baud Rate",    SCR_BAUD_RATE   },
    { "Device Info",  SCR_DEVICE_INFO },
};
#define SYSTEM_SET_COUNT  5

/* ===== 数值参数描述符 ===== */
typedef struct {
    float    min_val;
    float    max_val;
    float    step;
    uint8_t  decimals;
    const char *unit;
} num_desc_t;

static const num_desc_t c_num_desc[SCR_COUNT] = {
    /* 数值编辑屏幕 — 未指定的索引为 {0,0,0,0,""} (step=0 表示无效) */
    [SCR_METER_COEFF]  = { 0.001f,  99.999f,   0.001f, 3, "" },
    [SCR_MEDIUM_COEFF] = { 0.100f,  10.000f,   0.001f, 3, "" },
    [SCR_SMALL_SIGNAL] = { 0.0f,    10.0f,     0.1f,   1, "%" },
    [SCR_FILTER_TIME]  = { 0.1f,    100.0f,    0.1f,   1, "s" },
    [SCR_DAMPING_TIME] = { 0.1f,    100.0f,    0.1f,   1, "s" },
    [SCR_4MA_VALUE]    = { -9999.0f,99999.0f,  0.1f,   1, "" },
    [SCR_20MA_VALUE]   = { 0.1f,    99999.0f,  0.1f,   1, "" },
    [SCR_FREQ_OUTPUT]  = { 0.0f,    10000.0f,  1.0f,   0, "Hz" },
    [SCR_DENSITY]      = { 0.001f,  99999.0f,  0.1f,   1, "kg/m3" },
    [SCR_PIPE_DIA]     = { 0.1f,    99999.0f,  0.1f,   1, "mm" },
    [SCR_GAS_PRESS]    = { 0.0f,    99999.0f,  0.1f,   1, "KPa" },
    [SCR_GAS_TEMP]     = { -40.0f,  200.0f,    0.1f,   1, "C" },
    [SCR_REYNOLDS]     = { 0.001f,  10.000f,   0.001f, 3, "" },
    [SCR_TOTAL_FACTOR] = { 0.001f,  99.999f,   0.001f, 3, "" },
    [SCR_PRESET_TOTAL] = { 0.0f,    9999999.0f,0.1f,   1, "" },
    [SCR_SET_TOTAL]    = { 0.0f,    9999999.0f,0.1f,   1, "" },
    [SCR_DAC_ZERO]     = { 0.0f,    4095.0f,   1.0f,   0, "" },
    [SCR_DAC_FULL]     = { 0.0f,    4095.0f,   1.0f,   0, "" },
    [SCR_SPAN_ZERO]    = { 0.0f,    20.0f,     0.1f,   1, "mA" },
    [SCR_SPAN_FULL]    = { 0.0f,    20.0f,     0.1f,   1, "mA" },
    [SCR_COMM_ADDR]    = { 1.0f,    247.0f,    1.0f,   0, "" },
};

/* ===== 模块静态变量 ===== */
static nav_frame_t        s_nav_stack[NAV_STACK_DEPTH];
static int8_t             s_nav_depth;           /* -1 = 不活跃 */
static volatile uint16_t  s_idle_counter;        /* ISR 递增 */
static menu_config_t      s_config;
static access_level_t     s_access_level;

/* ===== 栈操作 ===== */
static void nav_push(menu_mode_t mode, screen_t scr)
{
    if (s_nav_depth < (int8_t)(NAV_STACK_DEPTH - 1)) {
        s_nav_depth++;
        memset(&s_nav_stack[s_nav_depth], 0, sizeof(nav_frame_t));
        s_nav_stack[s_nav_depth].mode = mode;
        s_nav_stack[s_nav_depth].screen_id = scr;
        /* confirm 默认选中 NO (安全) */
        if (mode == MODE_CONFIRM) s_nav_stack[s_nav_depth].confirm_sel = 0;
    }
}

static void nav_pop(void)
{
    if (s_nav_depth >= 0) s_nav_depth--;
}

/* ===== 列表数据获取 ===== */
static void get_list_data(screen_t scr, const list_item_t **pp_items, uint8_t *p_count)
{
    switch (scr) {
    case SCR_MAIN_MENU:    *pp_items = c_main_menu;    *p_count = MAIN_MENU_COUNT;    break;
    case SCR_BASIC_LIST:   *pp_items = c_basic_set;    *p_count = BASIC_SET_COUNT;    break;
    case SCR_OUTPUT_LIST:  *pp_items = c_output_set;   *p_count = OUTPUT_SET_COUNT;   break;
    case SCR_MEDIUM_LIST:  *pp_items = c_medium_set;   *p_count = MEDIUM_SET_COUNT;   break;
    case SCR_TOTALIZER_LIST: *pp_items = c_totalizer_set; *p_count = TOTALIZER_SET_COUNT; break;
    case SCR_ACCUM_LIST:   *pp_items = c_accum_set;    *p_count = ACCUM_SET_COUNT;    break;
    case SCR_CALIB_LIST:   *pp_items = c_calib_set;    *p_count = CALIB_SET_COUNT;    break;
    case SCR_SYSTEM_LIST:  *pp_items = c_system_set;   *p_count = SYSTEM_SET_COUNT;   break;
    default:               *pp_items = NULL;            *p_count = 0;                   break;
    }
}

/* ===== 模式判定 ===== */
static menu_mode_t detect_mode(screen_t scr)
{
    /* 枚举屏幕 */
    if (scr == SCR_STD_COND || scr == SCR_FLOW_UNIT ||
        scr == SCR_TOTAL_UNIT || scr == SCR_PULSE_EQUIV ||
        scr == SCR_BAUD_RATE || scr == SCR_LANGUAGE)
        return MODE_ENUM;

    /* 只读屏幕 */
    if (scr == SCR_FWD_TOTAL || scr == SCR_REV_TOTAL ||
        scr == SCR_NET_TOTAL || scr == SCR_DEVICE_INFO)
        return MODE_READONLY;

    /* 确认对话框 */
    if (scr == SCR_CLEAR_TOTALS || scr == SCR_FACTORY_RST)
        return MODE_CONFIRM;

    /* 子菜单列表 */
    if (scr == SCR_BASIC_LIST || scr == SCR_OUTPUT_LIST ||
        scr == SCR_MEDIUM_LIST || scr == SCR_TOTALIZER_LIST ||
        scr == SCR_ACCUM_LIST || scr == SCR_CALIB_LIST ||
        scr == SCR_SYSTEM_LIST || scr == SCR_MAIN_MENU)
        return MODE_LIST;

    /* 数值编辑: 检查描述符是否有效 */
    if ((uint8_t)scr < SCR_COUNT && c_num_desc[(uint8_t)scr].step != 0.0f)
        return MODE_NUMERIC;

    /* 降级为列表 */
    return MODE_LIST;
}

/* ===== param_storage 桥接 ===== */
static float load_param_val(screen_t scr)
{
    switch (scr) {
    case SCR_METER_COEFF:  return param_get_meter_coeff();
    case SCR_MEDIUM_COEFF: return param_get_medium_coeff();
    case SCR_SMALL_SIGNAL: return param_get_small_signal();
    case SCR_FILTER_TIME:  return param_get_filter_time();
    case SCR_DAMPING_TIME: return param_get_damping_time();
    case SCR_4MA_VALUE:    return param_get_value_4ma();
    case SCR_20MA_VALUE:   return param_get_value_20ma();
    case SCR_FREQ_OUTPUT:  return param_get_freq_output();
    case SCR_DENSITY:      return param_get_medium_density();
    case SCR_PIPE_DIA:     return param_get_pipe_diameter();
    case SCR_GAS_PRESS:    return param_get_gas_ref_press();
    case SCR_GAS_TEMP:     return param_get_gas_ref_temp();
    case SCR_REYNOLDS:     return param_get_reynolds_k();
    case SCR_TOTAL_FACTOR: return param_get_total_factor();
    case SCR_PRESET_TOTAL: return param_get_preset_total();
    case SCR_SET_TOTAL:    return param_get_forward_total();
    case SCR_DAC_ZERO:     return (float)DacZeroValue;
    case SCR_DAC_FULL:     return (float)DacFullValue;
    case SCR_SPAN_ZERO:    return SpanLoValue;
    case SCR_SPAN_FULL:    return SpanHiValue;
    case SCR_COMM_ADDR:    return (float)param_get_modbus_addr();
    default:               return 0.0f;
    }
}

static void save_param_val(screen_t scr, float val)
{
    switch (scr) {
    case SCR_METER_COEFF:  param_set_meter_coeff(val);   break;
    case SCR_MEDIUM_COEFF: param_set_medium_coeff(val);  break;
    case SCR_SMALL_SIGNAL: param_set_small_signal(val);  break;
    case SCR_FILTER_TIME:  param_set_filter_time(val);   break;
    case SCR_DAMPING_TIME: param_set_damping_time(val);  break;
    case SCR_4MA_VALUE:    param_set_value_4ma(val);     break;
    case SCR_20MA_VALUE:   param_set_value_20ma(val);    break;
    case SCR_FREQ_OUTPUT:  param_set_freq_output(val);   break;
    case SCR_DENSITY:      param_set_medium_density(val); break;
    case SCR_PIPE_DIA:     param_set_pipe_diameter(val); break;
    case SCR_GAS_PRESS:    param_set_gas_ref_press(val); break;
    case SCR_GAS_TEMP:     param_set_gas_ref_temp(val);  break;
    case SCR_REYNOLDS:     param_set_reynolds_k(val);    break;
    case SCR_TOTAL_FACTOR: param_set_total_factor(val);  break;
    case SCR_PRESET_TOTAL: param_set_preset_total(val);  break;
    case SCR_SET_TOTAL:    param_set_forward_total(val); break;
    case SCR_DAC_ZERO:     DacZeroValue = (uint16_t)val;
                           WriteBufferFlash_16(2, DAC_FLASH_PAGE_ADDR, DacValueBuf); break;
    case SCR_DAC_FULL:     DacFullValue = (uint16_t)val;
                           WriteBufferFlash_16(2, DAC_FLASH_PAGE_ADDR, DacValueBuf); break;
    case SCR_SPAN_ZERO:    param_set_value_4ma(val);    SpanLoValue = val;
                           { uint32_t bk[2];
                             bk[0] = ((uint32_t)SpanValueBuf[0].str[0] << 24) | ((uint32_t)SpanValueBuf[0].str[1] << 16) |
                                      ((uint32_t)SpanValueBuf[0].str[2] << 8)  | ((uint32_t)SpanValueBuf[0].str[3]);
                             bk[1] = ((uint32_t)SpanValueBuf[1].str[0] << 24) | ((uint32_t)SpanValueBuf[1].str[1] << 16) |
                                      ((uint32_t)SpanValueBuf[1].str[2] << 8)  | ((uint32_t)SpanValueBuf[1].str[3]);
                             WriteBufferFlash(2, ADDR_FLASH_PAGE_63, bk); } break;
    case SCR_SPAN_FULL:    param_set_value_20ma(val);   SpanHiValue = val;
                           { uint32_t bk[2];
                             bk[0] = ((uint32_t)SpanValueBuf[0].str[0] << 24) | ((uint32_t)SpanValueBuf[0].str[1] << 16) |
                                      ((uint32_t)SpanValueBuf[0].str[2] << 8)  | ((uint32_t)SpanValueBuf[0].str[3]);
                             bk[1] = ((uint32_t)SpanValueBuf[1].str[0] << 24) | ((uint32_t)SpanValueBuf[1].str[1] << 16) |
                                      ((uint32_t)SpanValueBuf[1].str[2] << 8)  | ((uint32_t)SpanValueBuf[1].str[3]);
                             WriteBufferFlash(2, ADDR_FLASH_PAGE_63, bk); } break;
    case SCR_COMM_ADDR:    param_set_modbus_addr((uint16_t)val);
                           bsp_usart_set_modbus_addr((uint16_t)val); break;
    default: break;
    }
}

static uint8_t load_enum_idx(screen_t scr)
{
    switch (scr) {
    case SCR_STD_COND:    return param_get_std_cond();
    case SCR_FLOW_UNIT:   return param_get_flow_unit();
    case SCR_TOTAL_UNIT:  return param_get_total_unit();
    case SCR_PULSE_EQUIV: return param_get_pulse_equiv();
    case SCR_BAUD_RATE:   return param_get_baud_rate();
    case SCR_LANGUAGE:    return param_get_language();
    default:              return 0;
    }
}

static void save_enum_idx(screen_t scr, uint8_t idx)
{
    switch (scr) {
    case SCR_STD_COND:    param_set_std_cond(idx);    break;
    case SCR_FLOW_UNIT:   param_set_flow_unit(idx);   break;
    case SCR_TOTAL_UNIT:  param_set_total_unit(idx);  break;
    case SCR_PULSE_EQUIV: param_set_pulse_equiv(idx); break;
    case SCR_BAUD_RATE:   param_set_baud_rate(idx); bsp_usart2_apply_uart_config(param_get_uart_config()); break;
    case SCR_LANGUAGE:    param_set_language(idx);    break;
    default: break;
    }
}

static float load_readonly_val(screen_t scr)
{
    switch (scr) {
    case SCR_FWD_TOTAL: return param_get_forward_total();
    case SCR_REV_TOTAL: return param_get_reverse_total();
    case SCR_NET_TOTAL: return param_get_forward_total() - param_get_reverse_total();
    default:            return 0.0f;
    }
}

static uint8_t is_coeff_screen(screen_t scr)
{
    return (uint8_t)(scr == SCR_METER_COEFF || scr == SCR_MEDIUM_COEFF);
}

static void get_numeric_range(screen_t scr, float *p_min, float *p_max)
{
    const num_desc_t *desc = &c_num_desc[scr];

    *p_min = desc->min_val;
    *p_max = desc->max_val;
    if (is_coeff_screen(scr) && s_access_level == ACCESS_OPERATOR) {
        *p_min = OPERATOR_COEFF_MIN;
        *p_max = OPERATOR_COEFF_MAX;
    }
}

static void coeff_digits_from_value(nav_frame_t *f, float val)
{
    uint32_t scaled = (uint32_t)(val * 1000.0f + 0.5f);

    f->coeff_digits[0] = (uint8_t)((scaled / 10000U) % 10U);
    f->coeff_digits[1] = (uint8_t)((scaled / 1000U) % 10U);
    f->coeff_digits[2] = (uint8_t)((scaled / 100U) % 10U);
    f->coeff_digits[3] = (uint8_t)((scaled / 10U) % 10U);
    f->coeff_digits[4] = (uint8_t)(scaled % 10U);
}

static float coeff_value_from_digits(const nav_frame_t *f)
{
    uint32_t scaled = (uint32_t)f->coeff_digits[0] * 10000U
                    + (uint32_t)f->coeff_digits[1] * 1000U
                    + (uint32_t)f->coeff_digits[2] * 100U
                    + (uint32_t)f->coeff_digits[3] * 10U
                    + (uint32_t)f->coeff_digits[4];

    return (float)scaled / 1000.0f;
}

/* ===== 初始化编辑状态 ===== */
static void init_mode_state(nav_frame_t *f)
{
    if (f->mode == MODE_NUMERIC) {
        f->edit_val = load_param_val(f->screen_id);
        if (is_coeff_screen(f->screen_id)) {
            coeff_digits_from_value(f, f->edit_val);
        }
    } else if (f->mode == MODE_ENUM) {
        f->enum_val = load_enum_idx(f->screen_id);
    }
    /* MODE_LIST: cursor/scroll 已由 memset 清零 */
    /* MODE_CONFIRM: confirm_sel 已在 nav_push 中设为 0 (NO) */
}

/* ===== 标题获取 ===== */
static const char *get_screen_title(screen_t scr)
{
    switch (scr) {
    case SCR_MAIN_MENU:    return "Main Menu";
    case SCR_BASIC_LIST:   return "Basic Setting";
    case SCR_STD_COND:     return "Std Cond";
    case SCR_METER_COEFF:  return "Meter Coeff";
    case SCR_MEDIUM_COEFF: return "Medium Coeff";
    case SCR_FLOW_UNIT:    return "Flow Unit";
    case SCR_TOTAL_UNIT:   return "Total Unit";
    case SCR_SMALL_SIGNAL: return "Small Sig Cut";
    case SCR_FILTER_TIME:  return "Filter Parm";
    case SCR_DAMPING_TIME: return "Damping Time";
    case SCR_OUTPUT_LIST:  return "Output Set";
    case SCR_4MA_VALUE:    return "4mA Value";
    case SCR_20MA_VALUE:   return "20mA Value";
    case SCR_FREQ_OUTPUT:  return "Freq Output";
    case SCR_PULSE_EQUIV:  return "Pulse Equiv";
    case SCR_MEDIUM_LIST:  return "Medium/Cond";
    case SCR_DENSITY:      return "Density";
    case SCR_PIPE_DIA:     return "Pipe Dia";
    case SCR_GAS_PRESS:    return "Gas Press";
    case SCR_GAS_TEMP:     return "Gas Temp";
    case SCR_REYNOLDS:     return "Reynolds K";
    case SCR_TOTALIZER_LIST: return "Totalizer Set";
    case SCR_TOTAL_FACTOR: return "Total Factor";
    case SCR_PRESET_TOTAL: return "Preset Total";
    case SCR_ACCUM_LIST:   return "Total Accum";
    case SCR_FWD_TOTAL:    return "Forward Total";
    case SCR_REV_TOTAL:    return "Reverse Total";
    case SCR_NET_TOTAL:    return "Net Total";
    case SCR_CLEAR_TOTALS: return "Clear All?";
    case SCR_SET_TOTAL:    return "Set Total";
    case SCR_CALIB_LIST:   return "Calibration";
    case SCR_DAC_ZERO:     return "DAC Zero";
    case SCR_DAC_FULL:     return "DAC Full";
    case SCR_SPAN_ZERO:    return "Span Zero";
    case SCR_SPAN_FULL:    return "Span Full";
    case SCR_SYSTEM_LIST:  return "System";
    case SCR_FACTORY_RST:  return "Factory Reset?";
    case SCR_COMM_ADDR:    return "Comm Addr";
    case SCR_BAUD_RATE:    return "Baud Rate";
    case SCR_DEVICE_INFO:  return "Device Info";
    case SCR_LANGUAGE:     return "Language";
    default:               return "Menu";
    }
}

/* ===== 前向声明 ===== */
static void render_current_frame(void);
static void handle_list(key_event_t evt);
static void handle_numeric(key_event_t evt);
static void handle_enum(key_event_t evt);
static void handle_password(key_event_t evt);
static void handle_readonly(key_event_t evt);
static void handle_confirm(key_event_t evt);

/* ===== 语言支持 (仅英文, 中文已移除以释放 Flash) ===== */

/* 语言枚举字符串 - 保留单选项以兼容菜单 SCR_LANGUAGE 屏幕 */
static const char * const s_language_str[LANG_COUNT] = { "English" };

/* ===== 渲染: M1 列表 ===== */
static void render_list(nav_frame_t *f)
{
    ssd1306_Fill(Black);

#ifdef SSD1306_INCLUDE_FONT_6x8
    const list_item_t *items;
    uint8_t count;
    char buf[22];
    uint8_t i;
    const int is_main_menu = (f->screen_id == SCR_MAIN_MENU);
    const int is_secondary_menu =
        (f->screen_id == SCR_BASIC_LIST ||
         f->screen_id == SCR_ACCUM_LIST ||
         f->screen_id == SCR_CALIB_LIST ||
         f->screen_id == SCR_SYSTEM_LIST);
    const int is_level_3_or_4 = (s_nav_depth == 2 || s_nav_depth == 3);
    const int use_7x10_layout = is_main_menu || is_secondary_menu || is_level_3_or_4;
    uint8_t visible_rows = use_7x10_layout ? 5 : 6;

    get_list_data(f->screen_id, &items, &count);
    if (!items || count == 0) return;

    if (use_7x10_layout) {
#ifdef SSD1306_INCLUDE_FONT_7x10
        /* 1~4 级列表的新布局：7x10 黑底白字居中，标题不反色 */
        const char *title = get_screen_title(f->screen_id);
        uint8_t title_x = (uint8_t)((SSD1306_WIDTH - strlen(title) * 7U) / 2U);
        ssd1306_SetCursor(title_x, 0);
        ssd1306_WriteString((char *)title, Font_7x10, White);
#endif
    } else {
        /* 更深层列表保持原有 6x8 反色标题 */
        ssd1306_FillRectangle(0, 0, 127, 7, White);
        ssd1306_SetCursor(2, 0);
        ssd1306_WriteString((char *)get_screen_title(f->screen_id), Font_6x8, Black);
    }

    /* 滚动窗口 */
    if (f->cursor >= visible_rows)
        f->scroll = (uint8_t)(f->cursor - visible_rows + 1);
    else
        f->scroll = 0;

    /* 列表项 */
    for (i = 0; i < visible_rows && (f->scroll + i) < count; i++) {
        uint8_t y = use_7x10_layout
                  ? (uint8_t)(10 + i * 11)  /* Font_7x10 行高 10px + 1px */
                  : (uint8_t)(8 + i * 9);   /* Font_6x8 行高 8px + 1px */
        uint8_t idx = (uint8_t)(f->scroll + i);
        int is_sel = (idx == f->cursor);

        if (is_sel) {
            ssd1306_FillRectangle(0, y, 127,
                                  (uint8_t)(y + (use_7x10_layout ? 9 : 8)), White);
        }
        {
            const char *label = items[idx].label;
            uint8_t field_chars = use_7x10_layout ? 18 : 19;
            uint8_t max_label_chars = (uint8_t)(field_chars - 1);
            buf[0] = ' ';
            uint8_t llen = (uint8_t)strlen(label);
            if (llen > max_label_chars) llen = max_label_chars;
            (void)memcpy(buf + 1, label, llen);
            uint8_t total = 1 + llen;
            while (total < field_chars) buf[total++] = ' ';
            buf[total] = '\0';
        }
        ssd1306_SetCursor(2, y);
        if (use_7x10_layout) {
#ifdef SSD1306_INCLUDE_FONT_7x10
            ssd1306_WriteString(buf, Font_7x10, is_sel ? Black : White);
#endif
        } else {
            ssd1306_WriteString(buf, Font_6x8, is_sel ? Black : White);
        }
    }
#else
    (void)f;
#endif
}

/* ===== 渲染: M2 数值编辑 ===== */
static void render_numeric(nav_frame_t *f)
{
    const num_desc_t *desc = &c_num_desc[f->screen_id];
    float min_val;
    float max_val;
    char buf[32];
    char tmp[16];
    const char *title = get_screen_title(f->screen_id);
    uint8_t x_start;

    get_numeric_range(f->screen_id, &min_val, &max_val);

    ssd1306_Fill(Black);

#ifdef SSD1306_INCLUDE_FONT_7x10
    /* 标题：黑底白字居中，不使用选中效果 */
    x_start = (uint8_t)((SSD1306_WIDTH - strlen(title) * 7U) / 2U);
    ssd1306_SetCursor(x_start, 0);
    ssd1306_WriteString((char *)title, Font_7x10, White);

    if (is_coeff_screen(f->screen_id)) {
        static const uint8_t c_digit_x[COEFF_DIGIT_COUNT] = { 43, 50, 64, 71, 78 };
        uint8_t i;

        /* Coeff 固定为 00.000，当前编辑位反色显示 */
        for (i = 0; i < COEFF_DIGIT_COUNT; i++) {
            buf[0] = (char)('0' + f->coeff_digits[i]);
            buf[1] = '\0';
            if (i == f->cursor) {
                ssd1306_FillRectangle(c_digit_x[i], 14,
                                      (uint8_t)(c_digit_x[i] + 6), 23, White);
                ssd1306_SetCursor(c_digit_x[i], 14);
                ssd1306_WriteString(buf, Font_7x10, Black);
            } else {
                ssd1306_SetCursor(c_digit_x[i], 14);
                ssd1306_WriteString(buf, Font_7x10, White);
            }
        }
        ssd1306_SetCursor(57, 14);
        ssd1306_WriteString(".", Font_7x10, White);
    } else {
        /* 普通数值页保持固定步长编辑 */
        ftoa(f->edit_val, desc->decimals, buf, sizeof(buf));
        x_start = (uint8_t)((SSD1306_WIDTH - strlen(buf) * 7U) / 2U);
        ssd1306_SetCursor(x_start, 14);
        ssd1306_WriteString(buf, Font_7x10, White);
    }

    /* Min/Max 拆成两行，避免 7x10 每行 18 字符的宽度限制 */
    strcpy(buf, "Min:");
    ftoa(min_val, desc->decimals, tmp, sizeof(tmp));
    strcat(buf, tmp);
    ssd1306_SetCursor(0, 28);
    ssd1306_WriteString(buf, Font_7x10, White);

    strcpy(buf, "Max:");
    ftoa(max_val, desc->decimals, tmp, sizeof(tmp));
    strcat(buf, tmp);
    ssd1306_SetCursor(0, 39);
    ssd1306_WriteString(buf, Font_7x10, White);

    if (is_coeff_screen(f->screen_id)) {
        strcpy(buf, "Enter:Next/Save");
    } else {
        /* 步长 + 单位 */
        strcpy(buf, "Step:");
        ftoa(desc->step, desc->decimals, tmp, sizeof(tmp));
        strcat(buf, tmp);
        strcat(buf, " ");
        strcat(buf, desc->unit);
    }
    ssd1306_SetCursor(0, 50);
    ssd1306_WriteString(buf, Font_7x10, White);
#endif
}

/* ===== 渲染: M3 枚举选择 ===== */
static void render_enum(nav_frame_t *f)
{
    char buf[16];
    const char * const *opts = NULL;
    uint8_t opt_count = 0;
    const uint8_t visible_rows = 5;
    int8_t start;
    uint8_t i;

    /* 获取枚举选项 */
    switch (f->screen_id) {
    case SCR_STD_COND:    opts = param_get_std_cond_strings();    opt_count = STD_COND_COUNT;    break;
    case SCR_FLOW_UNIT:   opts = param_get_flow_unit_strings();   opt_count = FLOW_UNIT_COUNT;   break;
    case SCR_TOTAL_UNIT:  opts = param_get_total_unit_strings();  opt_count = TOTAL_UNIT_COUNT;  break;
    case SCR_PULSE_EQUIV: opts = param_get_pulse_equiv_strings(); opt_count = PULSE_EQUIV_COUNT; break;
    case SCR_BAUD_RATE:   opts = param_get_baud_rate_strings();   opt_count = BAUD_RATE_COUNT;   break;
    case SCR_LANGUAGE:    opts = s_language_str;                  opt_count = LANG_COUNT;        break;
    default: break;
    }
    if (!opts) return;

    ssd1306_Fill(Black);

#ifdef SSD1306_INCLUDE_FONT_7x10
    /* 标题：黑底白字居中，不反色 */
    {
        const char *title = get_screen_title(f->screen_id);
        uint8_t title_x = (uint8_t)((SSD1306_WIDTH - strlen(title) * 7U) / 2U);
        ssd1306_SetCursor(title_x, 0);
        ssd1306_WriteString((char *)title, Font_7x10, White);
    }

    /* 选项列表：每屏 5 个，尽量使当前项居中 */
    start = (int8_t)f->enum_val - 2;
    if (start < 0) start = 0;
    if (start + (int8_t)visible_rows > (int8_t)opt_count)
        start = (int8_t)opt_count - (int8_t)visible_rows;
    if (start < 0) start = 0;

    for (i = 0; i < visible_rows && (start + (int8_t)i) < (int8_t)opt_count; i++) {
        uint8_t y = (uint8_t)(10 + i * 11);
        int is_sel = ((uint8_t)(start + i) == f->enum_val);
        if (is_sel) {
            ssd1306_FillRectangle(0, y, 127, (uint8_t)(y + 9), White);
        }
        {
            const char *opt = opts[start + i];
            buf[0] = ' ';
            buf[1] = '\0';
            (void)strncat(buf, opt, sizeof(buf) - 2);
        }
        ssd1306_SetCursor(2, y);
        ssd1306_WriteString(buf, Font_7x10, is_sel ? Black : White);
    }
#endif
}

/* ===== 渲染: M6 密码输入 ===== */
static void render_password(nav_frame_t *f)
{
    char buf[16];

    ssd1306_Fill(Black);

    /* 错误倒计时中显示错误信息 */
    if (f->pwd_err_visible) {
#ifdef SSD1306_INCLUDE_FONT_11x18
        ssd1306_SetCursor(20, 12);  /* (128 - 8 x 11) / 2 */
        ssd1306_WriteString("Password", Font_11x18, White);
        ssd1306_SetCursor(31, 36);  /* (128 - 6 x 11) / 2 */
        ssd1306_WriteString("Error!", Font_11x18, White);
#endif
        return;
    }

#ifdef SSD1306_INCLUDE_FONT_11x18
    /* 标题 */
    ssd1306_SetCursor(22, 0);
    ssd1306_WriteString("Password", Font_11x18, White);

    /* 3 位数字: 光标位反色 */
    {
        uint8_t x_start = 40;  /* 3×16=48 像素, 居中: (128-48)/2=40 */
        uint8_t i;
        for (i = 0; i < 3; i++) {
            buf[0] = (char)('0' + f->pwd_digits[i]);
            buf[1] = '\0';
            if (i == f->cursor) {
                /* 编辑位反色 */
                ssd1306_FillRectangle(x_start, 22, (uint8_t)(x_start + 10), 39, White);
                ssd1306_SetCursor(x_start, 24);
                ssd1306_WriteString(buf, Font_11x18, Black);
            } else {
                ssd1306_SetCursor(x_start, 24);
                ssd1306_WriteString(buf, Font_11x18, White);
            }
            x_start += 16;
        }
    }
#endif

#ifdef SSD1306_INCLUDE_FONT_7x10
    /* 密码页的单独例外：13 char x 7px = 91px，居中显示 */
    ssd1306_SetCursor(18, 48);
    ssd1306_WriteString("Range:000~999", Font_7x10, White);
#endif
}

/* ===== 渲染: M4 只读显示 ===== */
static void render_readonly(nav_frame_t *f)
{
    char buf[24];
    float val = load_readonly_val(f->screen_id);
    const char *title = get_screen_title(f->screen_id);

    ssd1306_Fill(Black);

#ifdef SSD1306_INCLUDE_FONT_7x10
    /* 标题：黑底白字居中，不反色 */
    {
        uint8_t title_x = (uint8_t)((SSD1306_WIDTH - strlen(title) * 7U) / 2U);
        ssd1306_SetCursor(title_x, 0);
        ssd1306_WriteString((char *)title, Font_7x10, White);
    }

    /* 设备信息特殊处理 */
    if (f->screen_id == SCR_DEVICE_INFO) {
        strcpy(buf, "Addr:");
        Int2String((int)param_get_modbus_addr(), buf + 5);
        ssd1306_SetCursor(0, 18);
        ssd1306_WriteString(buf, Font_7x10, White);

        {
            const char * const *baud_strs = param_get_baud_rate_strings();
            strcpy(buf, "Baud:");
            (void)strncat(buf, baud_strs[param_get_baud_rate()], sizeof(buf) - 6);
            ssd1306_SetCursor(0, 31);
            ssd1306_WriteString(buf, Font_7x10, White);
        }

        ssd1306_SetCursor(0, 44);
        ssd1306_WriteString("FW:v1.0.0", Font_7x10, White);
    } else {
        const char *unit_str = param_get_total_unit_str(param_get_total_unit());
        uint8_t x_start;

        /* 累积值与单位组合后整体居中，避免单位覆盖数值 */
        ftoa(val, 1, buf, sizeof(buf));
        strcat(buf, " ");
        strcat(buf, unit_str);
        x_start = (uint8_t)((SSD1306_WIDTH - strlen(buf) * 7U) / 2U);
        ssd1306_SetCursor(x_start, 18);
        ssd1306_WriteString(buf, Font_7x10, White);

        ssd1306_SetCursor(25, 42);
        ssd1306_WriteString("[Read Only]", Font_7x10, White);
    }
#endif
}

/* ===== 渲染: M5 确认对话框 ===== */
static void render_confirm(nav_frame_t *f)
{
    ssd1306_Fill(Black);

#ifdef SSD1306_INCLUDE_FONT_7x10
    /* 标题：黑底白字居中，不反色 */
    {
        const char *title = get_screen_title(f->screen_id);
        uint8_t title_x = (uint8_t)((SSD1306_WIDTH - strlen(title) * 7U) / 2U);
        ssd1306_SetCursor(title_x, 0);
        ssd1306_WriteString((char *)title, Font_7x10, White);
    }

    /* 警告信息 */
    if (f->screen_id == SCR_CLEAR_TOTALS) {
        ssd1306_SetCursor(8, 16);
        ssd1306_WriteString("All totals will", Font_7x10, White);
        ssd1306_SetCursor(8, 28);
        ssd1306_WriteString("be reset to ZERO", Font_7x10, White);
    } else if (f->screen_id == SCR_FACTORY_RST) {
        ssd1306_SetCursor(8, 16);
        ssd1306_WriteString("All parameters", Font_7x10, White);
        ssd1306_SetCursor(8, 28);
        ssd1306_WriteString("will be DEFAULT", Font_7x10, White);
    }

    /* YES / NO 选项 */
    if (f->confirm_sel == 1) {
        /* YES 选中 */
        ssd1306_FillRectangle(8, 48, 58, 57, White);
        ssd1306_SetCursor(12, 48);
        ssd1306_WriteString("YES", Font_7x10, Black);
        ssd1306_SetCursor(72, 48);
        ssd1306_WriteString("NO", Font_7x10, White);
    } else {
        /* NO 选中 (安全默认) */
        ssd1306_SetCursor(12, 48);
        ssd1306_WriteString("YES", Font_7x10, White);
        ssd1306_FillRectangle(68, 48, 100, 57, White);
        ssd1306_SetCursor(72, 48);
        ssd1306_WriteString("NO", Font_7x10, Black);
    }
#endif
}

/* ===== 统一渲染入口 ===== */
static void render_current_frame(void)
{
    nav_frame_t *f;

    if (s_nav_depth < 0) return;
    f = &s_nav_stack[s_nav_depth];

    switch (f->mode) {
    case MODE_LIST:     render_list(f);     break;
    case MODE_NUMERIC:  render_numeric(f);  break;
    case MODE_ENUM:     render_enum(f);     break;
    case MODE_PASSWORD: render_password(f); break;
    case MODE_READONLY: render_readonly(f); break;
    case MODE_CONFIRM:  render_confirm(f);  break;
    }

    ssd1306_UpdateScreen();
}

/* ===== 处理: M1 列表 ===== */
static void handle_list(key_event_t evt)
{
    nav_frame_t *f = &s_nav_stack[s_nav_depth];
    const list_item_t *items;
    uint8_t count;

    get_list_data(f->screen_id, &items, &count);
    if (!items) return;

    switch (evt) {
    case KEY_UP:
        if (f->cursor > 0) f->cursor--;
        break;
    case KEY_DOWN:
        if (f->cursor < (uint8_t)(count - 1)) f->cursor++;
        break;
    case KEY_ENTER: {
        uint8_t target = items[f->cursor].target;
        if (f->screen_id == SCR_MAIN_MENU && f->cursor == 0) {
            /* "1.Display" → 返回运行显示 */
            menu_exit();
            return;
        }
        /* 判定模式并 push (入口已验证密码, 子菜单直接进入) */
        {
            menu_mode_t m = detect_mode((screen_t)target);
            nav_push(m, (screen_t)target);
            init_mode_state(&s_nav_stack[s_nav_depth]);
        }
        break;
    }
    case KEY_BACK:
        nav_pop();
        if (s_nav_depth < 0) { menu_exit(); return; }
        break;
    default:
        return;
    }
    render_current_frame();
}

/* ===== 处理: M2 数值编辑 ===== */
static void handle_numeric(key_event_t evt)
{
    nav_frame_t *f = &s_nav_stack[s_nav_depth];
    const num_desc_t *desc = &c_num_desc[f->screen_id];
    float min_val;
    float max_val;

    get_numeric_range(f->screen_id, &min_val, &max_val);

    if (is_coeff_screen(f->screen_id)) {
        switch (evt) {
        case KEY_UP:
            f->coeff_digits[f->cursor] =
                (uint8_t)((f->coeff_digits[f->cursor] + 1U) % 10U);
            break;
        case KEY_DOWN:
            f->coeff_digits[f->cursor] =
                (uint8_t)((f->coeff_digits[f->cursor] + 9U) % 10U);
            break;
        case KEY_ENTER:
            if (f->cursor < (COEFF_DIGIT_COUNT - 1U)) {
                f->cursor++;
            } else {
                float val = coeff_value_from_digits(f);
                if (val < min_val) val = min_val;
                if (val > max_val) val = max_val;
                save_param_val(f->screen_id, val);
                nav_pop();
                if (s_nav_depth < 0) { menu_exit(); return; }
            }
            break;
        case KEY_BACK:
            nav_pop();  /* 不保存 */
            if (s_nav_depth < 0) { menu_exit(); return; }
            break;
        default:
            return;
        }
        render_current_frame();
        return;
    }

    switch (evt) {
    case KEY_UP:
        f->edit_val += desc->step;
        if (f->edit_val > max_val) f->edit_val = max_val;
        break;
    case KEY_DOWN:
        f->edit_val -= desc->step;
        if (f->edit_val < min_val) f->edit_val = min_val;
        break;
    case KEY_ENTER:
        save_param_val(f->screen_id, f->edit_val);
        nav_pop();
        if (s_nav_depth < 0) { menu_exit(); return; }
        break;
    case KEY_BACK:
        nav_pop();  /* 不保存 */
        if (s_nav_depth < 0) { menu_exit(); return; }
        break;
    default:
        return;
    }
    render_current_frame();
}

/* ===== 处理: M3 枚举选择 ===== */
static void handle_enum(key_event_t evt)
{
    nav_frame_t *f = &s_nav_stack[s_nav_depth];
    uint8_t max_val = 0;

    /* 获取枚举最大值 */
    switch (f->screen_id) {
    case SCR_STD_COND:    max_val = (uint8_t)(STD_COND_COUNT - 1);    break;
    case SCR_FLOW_UNIT:   max_val = (uint8_t)(FLOW_UNIT_COUNT - 1);   break;
    case SCR_TOTAL_UNIT:  max_val = (uint8_t)(TOTAL_UNIT_COUNT - 1);  break;
    case SCR_PULSE_EQUIV: max_val = (uint8_t)(PULSE_EQUIV_COUNT - 1); break;
    case SCR_BAUD_RATE:   max_val = (uint8_t)(BAUD_RATE_COUNT - 1);   break;
    case SCR_LANGUAGE:    max_val = (uint8_t)(LANG_COUNT - 1);        break;
    default: break;
    }

    switch (evt) {
    case KEY_UP:
        if (f->enum_val > 0) f->enum_val--;
        break;
    case KEY_DOWN:
        if (f->enum_val < max_val) f->enum_val++;
        break;
    case KEY_ENTER:
        save_enum_idx(f->screen_id, f->enum_val);
        nav_pop();
        if (s_nav_depth < 0) { menu_exit(); return; }
        break;
    case KEY_BACK:
        nav_pop();
        if (s_nav_depth < 0) { menu_exit(); return; }
        break;
    default:
        return;
    }
    render_current_frame();
}

/* ===== 处理: M6 密码输入 ===== */
static void handle_password(key_event_t evt)
{
    nav_frame_t *f = &s_nav_stack[s_nav_depth];

    /* 错误倒计时中屏蔽按键 */
    if (f->pwd_err_visible) return;

    switch (evt) {
    case KEY_UP:
        f->pwd_digits[f->cursor] =
            (uint8_t)((f->pwd_digits[f->cursor] + 1) % 10);
        break;
    case KEY_DOWN:
        f->pwd_digits[f->cursor] =
            (uint8_t)((f->pwd_digits[f->cursor] + 9) % 10);
        break;
    case KEY_ENTER:
        if (f->cursor < 2) {
            f->cursor++;
        } else {
            /* 第 3 位: 验证密码 */
            uint16_t pwd = (uint16_t)(f->pwd_digits[0] * 100 +
                          f->pwd_digits[1] * 10 +
                          f->pwd_digits[2]);
            access_level_t access_level = ACCESS_NONE;
            uint8_t ok;

            if (pwd == param_get_pwd_engineer()) {
                access_level = ACCESS_DEVELOPER;
            } else if (pwd == param_get_pwd_operator()) {
                access_level = ACCESS_OPERATOR;
            }
            ok = (uint8_t)(access_level != ACCESS_NONE);
            if (ok) {
                /* 成功: 弹出密码帧, 推入目标 */
                s_access_level = access_level;
                nav_pop();  /* 弹出密码帧 */
                {
                    menu_mode_t m = detect_mode((screen_t)f->pwd_target);
                    nav_push(m, (screen_t)f->pwd_target);
                    init_mode_state(&s_nav_stack[s_nav_depth]);
                }
            } else {
                f->pwd_err_start_ms = HAL_GetTick();
                f->pwd_err_visible = 1;
                f->cursor = 0;
                memset(f->pwd_digits, 0, sizeof(f->pwd_digits));
            }
        }
        break;
    case KEY_BACK:
        nav_pop();
        if (s_nav_depth < 0) { menu_exit(); return; }
        break;
    default:
        return;
    }
    render_current_frame();
}

/* ===== 处理: M4 只读显示 ===== */
static void handle_readonly(key_event_t evt)
{
    switch (evt) {
    case KEY_ENTER:
    case KEY_BACK:
        nav_pop();
        if (s_nav_depth < 0) { menu_exit(); return; }
        render_current_frame();
        break;
    default:
        /* UP/DOWN 不响应, 避免意外退出 */
        break;
    }
}

/* ===== 处理: M5 确认对话框 ===== */
static void handle_confirm(key_event_t evt)
{
    nav_frame_t *f = &s_nav_stack[s_nav_depth];

    switch (evt) {
    case KEY_UP:
    case KEY_DOWN:
        f->confirm_sel = f->confirm_sel ? 0 : 1;
        break;
    case KEY_ENTER:
        if (f->confirm_sel == 1) {
            /* YES: 执行操作 */
            if (f->screen_id == SCR_CLEAR_TOTALS) {
                param_set_forward_total(0.0f);
                param_set_reverse_total(0.0f);
            } else if (f->screen_id == SCR_FACTORY_RST) {
                param_storage_reset_defaults();
                /* 参数缓存/Flash 已恢复默认值，同步更新当前运行中的通信配置。 */
                bsp_usart_set_modbus_addr(param_get_modbus_addr());
                bsp_usart2_apply_uart_config(param_get_uart_config());
                /* 同步复位 DAC/Span 到默认值 */
                DacZeroValue = 12100;
                DacFullValue = 60000;
                WriteBufferFlash_16(2, DAC_FLASH_PAGE_ADDR, DacValueBuf);
                SpanLoValue = 0.0f;
                SpanHiValue = 100.0f;
                { uint32_t bk[2]; union { float f; uint32_t u; } cvt;
                  cvt.f = 0.0f;   bk[0] = cvt.u;
                  cvt.f = 100.0f; bk[1] = cvt.u;
                  WriteBufferFlash(2, ADDR_FLASH_PAGE_63, bk); }
            }
        }
        nav_pop();
        if (s_nav_depth < 0) { menu_exit(); return; }
        break;
    case KEY_BACK:
        nav_pop();
        if (s_nav_depth < 0) { menu_exit(); return; }
        break;
    default:
        return;
    }
    render_current_frame();
}

/* ===== Public API ===== */

void menu_init(const menu_config_t *p_cfg)
{
    s_nav_depth = -1;
    s_access_level = ACCESS_NONE;
    s_config.idle_timeout_10ms = (p_cfg && p_cfg->idle_timeout_10ms) ? p_cfg->idle_timeout_10ms : 3000;
    s_idle_counter = 0;
}

uint8_t menu_process(key_event_t key_evt, menu_status_t *p_out)
{
    /* 1. 菜单未激活: KEY_ENTER 进入密码验证 */
    if (s_nav_depth < 0) {
        if (key_evt == KEY_ENTER) {
            nav_push(MODE_PASSWORD, SCR_PASSWORD);
            s_nav_stack[s_nav_depth].pwd_target = (uint8_t)SCR_MAIN_MENU;
            s_idle_counter = 0;
            render_current_frame();
        }
        if (p_out) { p_out->active = 0; p_out->screen_id = 0; p_out->mode = 0; }
        return (uint8_t)(s_nav_depth >= 0 ? 1 : 0);
    }

    /* 2. KEY_HOME: 强制退出 */
    if (key_evt == KEY_HOME) {
        menu_exit();
        if (p_out) { p_out->active = 0; p_out->screen_id = 0; p_out->mode = 0; }
        return 0;
    }

    /* 3. 空闲超时检查 */
    if (s_idle_counter >= s_config.idle_timeout_10ms) {
        menu_exit();
        if (p_out) { p_out->active = 0; p_out->screen_id = 0; p_out->mode = 0; }
        return 0;
    }

    /* 4. 密码错误提示到期后，在主循环恢复输入界面 */
    {
        nav_frame_t *f = &s_nav_stack[s_nav_depth];
        if (f->mode == MODE_PASSWORD &&
            f->pwd_err_visible &&
            (uint32_t)(HAL_GetTick() - f->pwd_err_start_ms) >= PASSWORD_ERROR_DISPLAY_MS) {
            f->pwd_err_visible = 0;
            render_current_frame();
        }
    }

    /* 5. KEY_NONE: 无按键事件 */
    if (key_evt == KEY_NONE) {
        if (p_out && s_nav_depth >= 0) {
            p_out->active = 1;
            p_out->screen_id = (uint8_t)s_nav_stack[s_nav_depth].screen_id;
            p_out->mode = (uint8_t)s_nav_stack[s_nav_depth].mode;
        }
        return 1;
    }

    /* 6. 有按键: 重置空闲计时 */
    s_idle_counter = 0;

    /* 7. 按模式分发 */
    {
        nav_frame_t *f = &s_nav_stack[s_nav_depth];
        switch (f->mode) {
        case MODE_LIST:     handle_list(key_evt);     break;
        case MODE_NUMERIC:  handle_numeric(key_evt);  break;
        case MODE_ENUM:     handle_enum(key_evt);     break;
        case MODE_PASSWORD: handle_password(key_evt); break;
        case MODE_READONLY: handle_readonly(key_evt); break;
        case MODE_CONFIRM:  handle_confirm(key_evt);  break;
        }
    }

    if (p_out && s_nav_depth >= 0) {
        p_out->active = 1;
        p_out->screen_id = (uint8_t)s_nav_stack[s_nav_depth].screen_id;
        p_out->mode = (uint8_t)s_nav_stack[s_nav_depth].mode;
    } else if (p_out) {
        p_out->active = 0;
        p_out->screen_id = 0;
        p_out->mode = 0;
    }
    return (uint8_t)(s_nav_depth >= 0 ? 1 : 0);
}

void menu_exit(void)
{
    s_nav_depth = -1;
    s_access_level = ACCESS_NONE;
    ssd1306_Fill(Black);
    ssd1306_UpdateScreen();
}

uint8_t menu_is_active(void)
{
    return (uint8_t)(s_nav_depth >= 0 ? 1 : 0);
}

void menu_tick_10ms(void)
{
    if (s_nav_depth >= 0) s_idle_counter++;
}
