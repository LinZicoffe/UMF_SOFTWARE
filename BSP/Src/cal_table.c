/**
 * @file    cal_table.c
 * @brief   七点流量标定 — 分段线性插值实现
 */
#include "cal_table.h"
#include "param_storage.h"

void cal_table_init(void)
{
    /* 无需额外初始化, 标定数据由 param_storage 管理 */
}

float cal_correct_flow(float flow)
{
    int i;

    if (!param_get_cal_enabled()) return flow;

    /* 用量程计算流量百分比 */
    float span = param_get_value_20ma() - param_get_value_4ma();
    if (span <= 0.0f) return flow;

    float pct = (flow - param_get_value_4ma()) / span * 100.0f;

    /* 边界钳位: 使用首尾标定点 */
    float pct_lo = param_get_cal_pct(0);
    float pct_hi = param_get_cal_pct(CAL_POINT_COUNT - 1);

    if (pct <= pct_lo) return flow * param_get_cal_k(0);
    if (pct >= pct_hi) return flow * param_get_cal_k(CAL_POINT_COUNT - 1);

    /* 查找区间 + 线性插值 */
    for (i = 0; i < CAL_POINT_COUNT - 1; i++) {
        float cur_pct  = param_get_cal_pct(i);
        float next_pct = param_get_cal_pct(i + 1);
        if (pct <= next_pct) {
            float seg = next_pct - cur_pct;
            if (seg <= 0.0f) return flow;  /* 防御: 非法百分比 */
            float frac = (pct - cur_pct) / seg;
            float k = param_get_cal_k(i)
                    + frac * (param_get_cal_k(i + 1) - param_get_cal_k(i));
            return flow * k;
        }
    }

    return flow;
}
