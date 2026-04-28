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
#define FlowRateValue   InputBuffer[0] // 瞬时流量,Modbus address 40001
#define FlowTemperature InputBuffer[1] // 温度,Modbus address 40003
#define FlowPressure    InputBuffer[2] // 压力,Modbus address 40005

extern uint64_t Cumulativeflow;
#define CumulativeflowAddress 40 // MODBUS ADDRESS 4X:40041

/* 模拟参数寄存器地址 */
#define SimSwitchAddress       48  /* 模拟总开关 (uint16, 1 reg) */
#define SimFlowRateAddress     50  /* 模拟瞬时流量 (float, 2 regs) */
#define SimTemperatureAddress  52  /* 模拟温度 (float, 2 regs) */
#define SimCumulativeAddress   54  /* 模拟累积流量 (float, 2 regs) */
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

/* 模拟参数 API — 自动选择真实值或模拟值 */
uint8_t              sim_is_active(void);
float                effective_flow_rate(void);
float                effective_temperature(void);
const unsigned char *effective_flow_sum_buf(const unsigned char *real_buf);

#endif
