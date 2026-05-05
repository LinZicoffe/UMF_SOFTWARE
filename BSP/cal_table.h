/**
 * @file    cal_table.h
 * @brief   七点流量标定 — 分段线性插值
 *
 * 上位机通过 Modbus 写入标定系数 (cal_k[0..6]) 和标定点百分比 (cal_pct[0..6])。
 * 本模块在信号链中修正流量值: corrected_flow = raw_flow * k
 */
#ifndef __CAL_TABLE_H
#define __CAL_TABLE_H

#include "stm32f1xx_hal.h"

#define CAL_POINT_COUNT  7

/* 初始化 (当前为空操作, 保留接口) */
void  cal_table_init(void);

/* 标定修正: 未启用时原值返回, 启用时按七点插值修正 */
float cal_correct_flow(float flow);

#endif /* __CAL_TABLE_H */
