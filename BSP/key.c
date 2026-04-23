/**
 * @file    key.c
 * @brief   按键驱动实现 — 消抖 + 组合键检测 (参照 coriolis_drive)
 *
 * 消抖策略: 连续 2 次扫描 (20ms) 一致即认为有效
 * 事件触发: 按键释放瞬间，根据按下期间累积的按键掩码判定事件
 * 优先级:   三键 > 双键 > 单键
 *
 * UMF 硬件引脚映射 (面板实际接线, 非 CubeMX 命名):
 *   K1 = K_MOV (PC15) → 向下    BIT_K1 = 0x01  (面板"向下"键)
 *   K2 = K_SUB (PA0)  → 确定    BIT_K2 = 0x04  (面板"确认"键)
 *   K3 = K_ADD (PA11) → 向上    BIT_K3 = 0x02  (面板"向上"键)
 *   组合键: K1+K2=返回上一级, K1+K2+K3=返回主界面
 */

#include "key.h"

/* ---------- 位掩码定义 (按面板功能 K1/K2/K3 编号) ---------- */
#define BIT_K1  0x01  /* K_MOV  (PC15) 面板"向下"键 */
#define BIT_K2  0x04  /* K_SUB  (PA0)  面板"确认"键 */
#define BIT_K3  0x02  /* K_ADD  (PA11) 面板"向上"键 */

/* ---------- 消抖参数 ---------- */
#define DEBOUNCE_THRESHOLD  2  /* 连续 N 次 (×10ms) 一致则有效 */

/* ---------- 内部状态 (static 封装) ---------- */
static volatile uint8_t  s_raw;             /* 当前原始 GPIO 读取 */
static          uint8_t  s_debounce_cnt;    /* 消抖计数器 */
static          uint8_t  s_stable;          /* 消抖后的稳态 */
static          uint8_t  s_pressed_mask;    /* 按下期间累积的按键位 (OR) */
static volatile key_event_t s_event;        /* 待取事件 (深度1) */

/* ---------- 内部函数 ---------- */

/**
 * @brief  读取 3 个 GPIO，组合为位掩码
 * @note   按下=LOW(0)，取反后按下=1
 */
static uint8_t read_keys(void)
{
    uint8_t mask = 0;
    /* PC15 = K_MOV, 面板"向下"键 → BIT_K1 */
    if (HAL_GPIO_ReadPin(K_MOV_GPIO_Port, K_MOV_Pin) == GPIO_PIN_RESET)
        mask |= BIT_K1;
    /* PA11 = K_ADD, 面板"向上"键 → BIT_K3 */
    if (HAL_GPIO_ReadPin(K_ADD_GPIO_Port, K_ADD_Pin) == GPIO_PIN_RESET)
        mask |= BIT_K3;
    /* PA0 = K_SUB, 面板"确认"键 → BIT_K2 */
    if (HAL_GPIO_ReadPin(K_SUB_GPIO_Port, K_SUB_Pin) == GPIO_PIN_RESET)
        mask |= BIT_K2;
    return mask;
}

/**
 * @brief  根据累积按键掩码判定事件（优先级: 三键 > 双键 > 单键）
 */
static key_event_t decode_event(uint8_t mask)
{
    /* 三键组合: K1+K2+K3 = 返回主界面 */
    if ((mask & (BIT_K1 | BIT_K2 | BIT_K3)) == (BIT_K1 | BIT_K2 | BIT_K3))
        return KEY_HOME;

    /* 双键组合: K1+K2 = 返回上一级 */
    if ((mask & (BIT_K1 | BIT_K2)) == (BIT_K1 | BIT_K2))
        return KEY_BACK;

    /* 单键 */
    if (mask & BIT_K1) return KEY_DOWN;    /* K1: 向下 */
    if (mask & BIT_K2) return KEY_ENTER;   /* K2: 确定 */
    if (mask & BIT_K3) return KEY_UP;      /* K3: 向上 */

    return KEY_NONE;
}

/* ---------- 公共 API ---------- */

void key_init(void)
{
    s_raw          = 0;
    s_debounce_cnt = 0;
    s_stable       = 0;
    s_pressed_mask = 0;
    s_event        = KEY_NONE;
}

void key_scan_10ms(void)
{
    uint8_t cur = read_keys();

    /* 消抖: 与上次一致则计数+1，否则重置 */
    if (cur == s_raw) {
        if (s_debounce_cnt < 255)
            s_debounce_cnt++;
    } else {
        s_debounce_cnt = 1;
        s_raw = cur;
    }

    /* 消抖通过后更新稳态 */
    if (s_debounce_cnt >= DEBOUNCE_THRESHOLD) {
        uint8_t prev_stable = s_stable;
        s_stable = cur;

        if (s_stable != 0) {
            /* 有键按下: 累积按键位 */
            s_pressed_mask |= s_stable;
        } else if (prev_stable != 0) {
            /* 从按下→释放: 产生事件 */
            s_event = decode_event(s_pressed_mask);
            s_pressed_mask = 0;
        }
    }
}

key_event_t key_get_event(void)
{
    key_event_t evt = s_event;
    s_event = KEY_NONE;
    return evt;
}
