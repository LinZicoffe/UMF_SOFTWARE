/*
 * @Author: liyongtai
 * @Date: 2022-04-19 13:01:26
 * @LastEditTime: 2024-10-26 10:38:09
 * @LastEditors: liyongtai
 * @Description: 串口空闲中断及DMA接收无定长数据
 * @FilePath: \UMF_SOFTWARE\BSP\bsp_usart.h
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __BSP_USART_H__
#define __BSP_USART_H__
/*------------------------Include Files -------------------------------------*/
#include "usart.h"
#include "mystring.h"
/* Exported types ------------------------------------------------------------*/

#define UART_RX_LEN 150 // 开辟一段UART接收缓存长度
#define UART_TX_LEN 150 // 开辟一段UART发送缓存长度

typedef struct
{
    uint8_t  RX_Flag : 1;           // receive flag
    uint16_t RX_Size;               // receive length
    uint8_t  RxBuffer[UART_RX_LEN]; // receive buffer
} Uart_RecTypeDef;

typedef struct
{
    uint8_t  TX_Flag : 1;           // Send data flag
    uint16_t TX_Size;               // Send data length
    uint8_t  TxBuffer[UART_TX_LEN]; // Send data buffer
} Uart_SendTypeDef;

typedef union
{
    uint8_t str[4];
    float   num;
} Uart_SendfloatTypeDef;

#define Uart3_Rx_Cnt 1

/* Private define ----------------------------------------------------------*/
extern uint8_t          Uart1RxBuffer[UART_RX_LEN]; // 数据处理区域
extern uint8_t          Uart2RxBuffer[UART_RX_LEN]; // 数据处理区域
extern Uart_RecTypeDef  Uart1ReceiveType;
extern Uart_RecTypeDef  Uart2ReceiveType;
extern Uart_SendTypeDef Uart1SendDataType;
extern Uart_SendTypeDef Uart2SendDataType;
extern uint8_t          FlowClearCmdFlag;         // 累积清零命令
extern uint8_t          FlowRstCmdFlag;           // 流量模组复位
extern uint8_t          FlowPassiveReadCmdFlag;
extern uint8_t          FlowPassiveReadCmdEnable; // TRUE:模组被动发送数据
extern uint8_t          FlowActiveReadCmdEnable;  // TRUE:模组主动发送数据
extern uint8_t ModuleState;//module state
#define InputBufferStartMinAddress 0x0            // MODBUS:03function code input zone
#define InputBufferStartMaxAddress 18
#define InputBufferLength          10
extern Uart_SendfloatTypeDef InputBuffer[10];
#define FlowRateValue   InputBuffer[0] // 瞬时流量,Modbus address 40001, 固定单位 L/h (BCD 解析已按帧 flag 换算, 不随 40023 Flow Unit 变化)
#define FlowTemperature InputBuffer[1] // 温度,Modbus address 40003
#define FlowPressure    InputBuffer[2] // 压力,Modbus address 40005

extern uint64_t Cumulativeflow;  // Modbus 40041, UFL-1A BCD 原始计数值, 固件未 ÷1000; LSB 由帧 byte[8] flag 决定 (0x0a=0.001L, 0x1a=0.001m³)
#define CumulativeflowAddress 40 // MODBUS ADDRESS 4X:40041

/* 运行参数寄存器地址 (寄存器 22~29) */
#define FlowUnitAddress     22  /* 流量单位 (uint16, enum 0~3) */
#define TotalUnitAddress    23  /* 累积单位 (uint16, enum 0~3) */
#define MeterCoeffAddress   24  /* 仪表系数 (float, 2 regs) */
#define MediumCoeffAddress  26  /* 介质系数 (float, 2 regs) */
#define SmallSignalAddress  28  /* 小信号切除 (float, 2 regs) */

/* 模拟参数寄存器地址 */
#define SimSwitchAddress       48  /* 模拟总开关 (uint16, 1 reg) */
#define SimFlowRateAddress     50  /* 模拟瞬时流量 (float, 2 regs) */
#define SimTemperatureAddress  52  /* 模拟温度 (float, 2 regs) */
#define SimCumulativeAddress   54  /* 模拟累积流量 (float, 2 regs) */

/* 扩展参数寄存器地址 — 第一批 (只读运行数据, 寄存器 60~68) */
#define RunStateAddr           60  /* 通信状态 ModuleState (uint16, R) */
#define FwdTotalAddr           61  /* 正向累积 forward_total (float, 2 regs, R) */
#define RevTotalAddr           63  /* 反向累积 reverse_total (float, 2 regs, R) */
#define NetTotalAddr           65  /* 净累积 (float, 2 regs, R) */
#define DacCurrentAddr         67  /* 实时 4-20mA 电流 (float, 2 regs, R) */

/* 扩展参数寄存器地址 — 第二批 (读写配置参数, 寄存器 69~86) */
#define StdCondAddr            69  /* 标准工况 std_cond (uint16, R/W) */
#define FilterTimeAddr         70  /* 滤波参数 filter_time (float, 2 regs, R/W) */
#define DampingTimeAddr        72  /* 阻尼时间 damping_time (float, 2 regs, R/W) */
#define FreqOutputAddr         74  /* 频率输出 freq_output (float, 2 regs, R/W) */
#define PulseEquivAddr         76  /* 脉冲当量 pulse_equiv (uint16, R/W) */
#define DensityAddr            77  /* 介质密度 medium_density (float, 2 regs, R/W) */
#define PipeDiaAddr            79  /* 管径 pipe_diameter (float, 2 regs, R/W) */
#define GasPressAddr           81  /* 气参压力 gas_ref_press (float, 2 regs, R/W) */
#define GasTempAddr            83  /* 气参温度 gas_ref_temp (float, 2 regs, R/W) */
#define ReynoldsAddr           85  /* 雷诺系数 reynolds_k (float, 2 regs, R/W) */

/* 扩展参数寄存器地址 — 第三批 (系统参数, 寄存器 87~94) */
#define TotalFactorAddr        87  /* 累积系数 total_factor (float, 2 regs, R/W) */
#define PresetTotalAddr        89  /* 预设总量 preset_total (float, 2 regs, R/W) */
#define CommAddrReg            91  /* 通信地址 modbus_addr (uint16, R/W) */
#define BaudRateReg            92  /* UART 配置 uart_config (uint16, R/W)
                                    *   bit 2:0 = 波特率索引 (0~5)
                                    *   bit 4:3 = 校验位 (0=无, 1=奇, 2=偶)
                                    *   bit 5   = 停止位 (0=1位, 1=2位) */
#define LanguageReg            93  /* 语言 language (uint16, R/W) */
#define OledRecoveryAddr       94  /* OLED 抗干扰自愈重初始化间隔 (uint16, R/W)
                                    *   单位: 100ms; 0=禁用; 默认 50=5s; 最大 600=60s */

/* 扩展参数寄存器地址 — 第四批 (七点标定, 寄存器 95~123) */
#define CalEnableAddr          95  /* 标定使能 (uint16, R/W) */
#define CalK0Addr              96  /* k[0] (float, 2 regs, R/W) */
#define CalK1Addr              98  /* k[1] (float, 2 regs, R/W) */
#define CalK2Addr             100  /* k[2] (float, 2 regs, R/W) */
#define CalK3Addr             102  /* k[3] (float, 2 regs, R/W) */
#define CalK4Addr             104  /* k[4] (float, 2 regs, R/W) */
#define CalK5Addr             106  /* k[5] (float, 2 regs, R/W) */
#define CalK6Addr             108  /* k[6] (float, 2 regs, R/W) */
#define CalPct0Addr           110  /* 标定点百分比 p[0], 默认 0.0 (float, 2 regs, R/W) */
#define CalPct1Addr           112  /* p[1], 默认 3.0 */
#define CalPct2Addr           114  /* p[2], 默认 10.0 */
#define CalPct3Addr           116  /* p[3], 默认 25.0 */
#define CalPct4Addr           118  /* p[4], 默认 50.0 */
#define CalPct5Addr           120  /* p[5], 默认 75.0 */
#define CalPct6Addr           122  /* p[6], 默认 100.0 */
#define CalEndAddr            123  /* 标定区结束地址 */

/* 扩展参数区范围 */
#define ExtParamStartAddr      60
#define ExtParamEndAddr       123

extern unsigned char strFlowSumBuf[20];
extern unsigned char strFlowRateBuf[20];
extern unsigned char         strFlowRate_2Buf[10];
extern unsigned char strFlowTemBuf[20];
extern unsigned char strFlowPressBuf[20];
extern uint8_t Sumunit;
extern uint8_t       GetCheckSum(uint8_t *ptr, uint8_t len);
extern void          EnableUart_IT_IDLE(UART_HandleTypeDef *huart, Uart_RecTypeDef *pBuf);
extern void          UartReceive_IDLE(UART_HandleTypeDef *huart, DMA_HandleTypeDef *hdma_uart_rx);
extern void          Uart1_Communication(void);
extern void          Uart2_Communication(void);
extern void          bsp_usart_set_modbus_addr(uint16_t addr);
extern void          bsp_usart2_apply_uart_config(uint8_t uart_config);
extern void          bsp_usart2_check_baud_rate_pending(void);

/* 模拟参数 API — 自动选择真实值或模拟值 */
uint8_t              sim_is_active(void);
float                effective_flow_rate(void);
float                effective_temperature(void);
const unsigned char *effective_flow_sum_buf(const unsigned char *real_buf);
float                convert_flow_rate_from_lph(float flow_lph, uint8_t target_unit, float density_kg_m3);

#endif
