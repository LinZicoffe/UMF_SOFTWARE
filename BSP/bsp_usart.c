
/*
 * @Author      : liyongtai
 * @Date        : 2022-04-20 17: 26: 33
 * @LastEditTime: 2024-11-14 12:48:07
 * @LastEditors: liyongtai
 * @Description : 串口空闲中断DMA接收
 * @FilePath: \UMF_SOFTWARE\BSP\bsp_usart.c
 */
/* Private includes ----------------------------------------------------------*/
#include "bsp_usart.h"
#include "param_storage.h"
#include "mystring.h"

#include "tim.h"
/* Private define ----------------------------------------------------------*/

/* ── 瞬时流量去极值滤波 (10 点滑动窗口, 去最大值) ────── */
#define FLOW_FILTER_N   10  /* 窗口样本数 */
static float    s_flt_window[FLOW_FILTER_N];
static uint8_t  s_flt_write_index;
static uint8_t  s_flt_count;
static float    s_flt_result;
static uint8_t  s_flt_valid;

static void flow_filter_feed(float sample)
{
    float sum;
    float max;
    uint8_t i;

    s_flt_window[s_flt_write_index] = sample;
    s_flt_write_index = (uint8_t)((s_flt_write_index + 1U) % FLOW_FILTER_N);

    if (s_flt_count < FLOW_FILTER_N) {
        s_flt_count++;
    }
    if (s_flt_count < FLOW_FILTER_N) {
        return;
    }

    sum = 0.0f;
    max = s_flt_window[0];
    for (i = 0; i < FLOW_FILTER_N; i++) {
        sum += s_flt_window[i];
        if (s_flt_window[i] > max) {
            max = s_flt_window[i];
        }
    }

    s_flt_result = (sum - max) / (float)(FLOW_FILTER_N - 1);
    s_flt_valid = 1;
}

/* ── 常量定义 ─────────────────────────────────────── */
#define PREAMBLE             0XFE
#define STARTCMD             0X11
#define EOFbyte              0x16
#define FLWSetReadCmd        0x5c
#define FLWSetActiveReadPra  0x00
#define FLWSetPassiveReadPra 0x01
#define FLWStartReadCmd      0x5b
#define FLWStartReadLongPra  0xcb
#define FLWStartReadShortPra 0xfd
#define FLWClearCmd          0x5a
#define FLWClearCmdPra       0xfd
#define FLWRstCmd            0x5d
#define FLWRstCmdPra         0xcb
/* Private variables ---------------------------------------------------------*/
Uart_RecTypeDef  Uart1ReceiveType;
Uart_RecTypeDef  Uart2ReceiveType;
Uart_SendTypeDef Uart1SendDataType;
Uart_SendTypeDef Uart2SendDataType;
uint8_t          Uart1RxBuffer[UART_RX_LEN]; // 数据处理区域
volatile uint8_t Uart1HaveData;
volatile uint8_t Uart1RxCounter;
uint8_t          Uart2RxBuffer[UART_RX_LEN]; // 数据处理区域
volatile uint8_t Uart2HaveData;
volatile uint8_t Uart2RxCounter;

	Uart_SendfloatTypeDef InputBuffer[10]; /*input区域共40个字节*/
uint64_t              Cumulativeflow;  // MODBUS ADDRESS 4X:40041
unsigned char         strFlowSumBuf[20];
unsigned char         strFlowRateBuf[20];
unsigned char         strFlowTemBuf[20];
unsigned char         strFlowPressBuf[20];
unsigned char         strFlowRate_2Buf[10];
/*output 40个字节区域*/

uint8_t FlowClearCmdFlag;         // 累积清零命令
uint8_t FlowRstCmdFlag;           // 流量模组复位
uint8_t FlowPassiveReadCmdFlag;
uint8_t FlowPassiveReadCmdEnable; // TRUE:模组被动发送数据
uint8_t FlowActiveReadCmdEnable;  // TRUE:模组主动发送数据
uint8_t Sumunit;
/* Private define ------------------------------------------------------------*/
static uint16_t s_modbus_addr = 2;   /* Modbus 从站地址, 可通过 bsp_usart_set_modbus_addr() 修改 */

/* UART 配置切换 — 延迟应用机制 (确保 Modbus 响应在旧配置下发送完成) */
static volatile uint8_t  s_baud_rate_pending;     /* 非0表示有待应用的 UART 配置变更 */
static          uint8_t  s_uart_cfg_pending;      /* 待应用的 packed uart_config */

/* 波特率索引 → 实际频率查找表 */
static const uint32_t s_baud_table[BAUD_RATE_COUNT] = {
    4800, 9600, 19200, 38400, 115200, 2400
};

/* 模拟参数 — static 内部变量 */
static uint16_t s_sim_switch = 0;
static Uart_SendfloatTypeDef s_sim_flow_rate;
static Uart_SendfloatTypeDef s_sim_temperature;
static Uart_SendfloatTypeDef s_sim_cumulative;
static unsigned char s_sim_flow_sum_buf[20];
/* 运行参数 — FC06 分次写入缓冲 */
static Uart_SendfloatTypeDef s_meter_coeff_buf;
static Uart_SendfloatTypeDef s_medium_coeff_buf;
static Uart_SendfloatTypeDef s_small_signal_buf;
/* 扩展参数 — FC06 分次写入缓冲 (float 参数) */
static Uart_SendfloatTypeDef s_filter_time_buf;
static Uart_SendfloatTypeDef s_damping_time_buf;
static Uart_SendfloatTypeDef s_freq_output_buf;
static Uart_SendfloatTypeDef s_density_buf;
static Uart_SendfloatTypeDef s_pipe_dia_buf;
static Uart_SendfloatTypeDef s_gas_press_buf;
static Uart_SendfloatTypeDef s_gas_temp_buf;
static Uart_SendfloatTypeDef s_reynolds_buf;
static Uart_SendfloatTypeDef s_total_factor_buf;
static Uart_SendfloatTypeDef s_preset_total_buf;
/* 标定参数 — FC06 分次写入缓冲 */
static Uart_SendfloatTypeDef s_cal_k_buf[7];
static Uart_SendfloatTypeDef s_cal_pct_buf[7];
#define FlowMeterReadDataCommand        0x03   // 读取1或者多字节寄存器数据
#define FlowMeterWriteSingleDataCommand 0x06   // 写1字寄存器数据
#define FlowMeterWriteMultiDataCommand  0x10   // 写多字寄存器数据
#define FlowRateAddress                 0x0000 // 流量计瞬时流量地址
#define CumulativeFlowAddress           0x0002 // 累积流量
#define FlowMeterPressAddress           0x0005
#define FlowMeterTempAddress            0x000c
#define SetValueFlowAddress             0xA0A0 // 控制阀流量设定数值
#define ValveConOpenAddress             0xA0A1 // 阀开地址
#define ValveConCloseAddress            0xA0A2 // 阀关地址
#define ValveConCommand                 0x7410 // 阀控制命令
uint8_t  ModuleState;                          // module state
uint32_t ModuleRecTimes;
/* Private function prototypes -----------------------------------------------*/
void            Uart1_Communication(void);
void            Uart2_Communication(void);
void            Uart1_Send_Function(void);
void            Uart1_Receive_Function(void);
void            Modbus_Function_1(void);
void            Modbus_Function_3(void);
void            Modbus_Function_4(void);
void            Modbus_Function_5(void);
void            Modbus_Function_6(void);
void            Modbus_Function_10(void);
uint8_t         GetCheckSum(uint8_t *ptr, uint8_t len);
uint16_t        SWAPWORD(uint16_t word);
float           raw2ieee(uint8_t *raw);
static uint8_t  BCD2DEC(uint8_t bcd);
static float    BCDTOInt(uint32_t bcd);
static uint64_t BCD_TO_LongInt(uint64_t bcd);
static void     sim_format_cumulative(float value);
static float    compute_dac_current_mA(void);
static uint64_t mul_div_round_u64(uint64_t value, uint32_t multiplier, uint64_t divisor);
static uint64_t convert_total_to_milli_unit(uint64_t raw_total, uint8_t source_unit,
                                            uint8_t target_unit, float density_kg_m3);
static void     format_milli_total(uint64_t milli_value, unsigned char *p_buf,
                                   uint8_t buf_size);

uint8_t  BCDtoStr(unsigned char *str, unsigned char *BCD, int BCD_length);
uint16_t getCRC16(uint8_t *ptr, uint8_t len);
void     EnableUart_IT_IDLE(UART_HandleTypeDef *huart, Uart_RecTypeDef *pBuf);
void     UartReceive_IDLE(UART_HandleTypeDef *huart, DMA_HandleTypeDef *hdma_uart_rx);
/*****************************************************************************
 * 函 数 名  : EnableUart_IT
 * 函数功能  : 使能串口空闲中断
 * 输入参数  :huart UART handle;Uart_RecTypeDef:数据缓存区
 * 返 回 值  :none
 * 其    它  :
 *****************************************************************************/
void EnableUart_IT_IDLE(UART_HandleTypeDef *huart, Uart_RecTypeDef *pBuf)
{
    __HAL_UART_CLEAR_IDLEFLAG(huart);
    __HAL_UART_CLEAR_OREFLAG(huart);
    HAL_UART_Receive_DMA(huart, pBuf->RxBuffer, UART_RX_LEN);
    __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);
}

/*****************************************************************************
 * 函 数 名  : UartReceive_IDLE
 * 函数功能  : 串口接受空闲中断处理函数
 * 输入参数  : UART_HandleTypeDef *huart  串口句柄
 * 返 回 值  :
 * 其    它  :
 *****************************************************************************/
void UartReceive_IDLE(UART_HandleTypeDef *huart, DMA_HandleTypeDef *hdma_uart_rx)
{
    uint32_t temp;
    uint8_t  i;
    if ((__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE) != RESET))
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_IDLEFLAG(huart);
        temp = huart->Instance->SR;
        temp = huart->Instance->DR;
        HAL_UART_DMAStop(huart);

        if (huart->Instance == USART1)
        {
            temp = __HAL_DMA_GET_COUNTER(hdma_uart_rx);
            if (temp <= UART_RX_LEN)
                Uart1ReceiveType.RX_Size = UART_RX_LEN - temp;
            else
                Uart1ReceiveType.RX_Size = 0;
            Uart1ReceiveType.RX_Flag = 1;
            /* 背压策略: 上一帧未处理完时不重启 DMA, 避免覆盖缓冲区 */
            if (!Uart1HaveData)
            {
                Uart1HaveData = 1;
                for (i = 0; i < Uart1ReceiveType.RX_Size; i++)
                {
                    Uart1RxBuffer[i] = Uart1ReceiveType.RxBuffer[i];
                }
                Uart1RxCounter = Uart1ReceiveType.RX_Size;
                HAL_UART_Receive_DMA(huart, Uart1ReceiveType.RxBuffer, UART_RX_LEN);
            }
        }
        if (huart->Instance == USART2)
        {
            temp = __HAL_DMA_GET_COUNTER(hdma_uart_rx);
            if (temp <= UART_RX_LEN)
                Uart2ReceiveType.RX_Size = UART_RX_LEN - temp;
            else
                Uart2ReceiveType.RX_Size = 0;
            Uart2ReceiveType.RX_Flag = 1;
            /* 背压策略: 上一帧未处理完时不重启 DMA, 避免覆盖缓冲区 */
            if (!Uart2HaveData)
            {
                Uart2HaveData = 1;
                for (i = 0; i < Uart2ReceiveType.RX_Size; i++)
                {
                    Uart2RxBuffer[i] = Uart2ReceiveType.RxBuffer[i];
                }
                Uart2RxCounter = Uart2ReceiveType.RX_Size;
                HAL_UART_Receive_DMA(huart, Uart2ReceiveType.RxBuffer, UART_RX_LEN);
            }
        }
    }
}
/**
 * @Author: liyongtai
 * @description:串口发送完成回调函数,使用DMA时需要将发送完成状态更新，否则DMA会出现bug
 * @param {UART_HandleTypeDef} *huart
 * @return {*}
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    huart->gState = HAL_UART_STATE_READY; // update

    if (huart == &huart1)
    {
        __HAL_DMA_CLEAR_FLAG(&hdma_usart1_tx, DMA_FLAG_TC4);
    }
    if (huart == &huart2)
    {
        __HAL_DMA_CLEAR_FLAG(&hdma_usart2_tx, DMA_FLAG_TC4);
        Time_Delay(10);
        HAL_GPIO_WritePin(USART2_DE_GPIO_Port, USART2_DE_Pin, GPIO_PIN_RESET);
    }
}
/**
 * @Author: liyongtai
 * @description: 串口1处理函数
 * @return {*}
 */
void Uart1_Communication(void)
{
    Uart1_Send_Function();
    Uart1_Receive_Function();
}
/*****************************************************************************
 * 函 数 名  : Uart1_Send_Function
 * 函数功能  : 串口1发送完成函数
 * 输入参数  : UART_HandleTypeDef *huart  串口操作句柄
 * 返 回 值  :
 * 其    它  :使用DMA时需要将发送完成状态更新，否则DMA会出现bug
 *****************************************************************************/

void Uart1_Send_Function(void)
{
    uint8_t Comm1TaskFlag = 0; // 串口1发送任务标志
    uint8_t checkbuffer[2];
    if ((!Comm1TaskFlag) && FlowClearCmdFlag)
    {
        Comm1TaskFlag                 = 1;
        FlowClearCmdFlag              = 0;
        Uart1SendDataType.TxBuffer[0] = PREAMBLE;
        Uart1SendDataType.TxBuffer[1] = PREAMBLE;
        Uart1SendDataType.TxBuffer[2] = STARTCMD;
        Uart1SendDataType.TxBuffer[3] = FLWClearCmd;
        Uart1SendDataType.TxBuffer[4] = FLWClearCmdPra;
        checkbuffer[0]                = FLWClearCmd;
        checkbuffer[1]                = FLWClearCmdPra;
        Uart1SendDataType.TxBuffer[5] = GetCheckSum(checkbuffer, 2);
        Uart1SendDataType.TxBuffer[6] = EOFbyte;
        Uart1SendDataType.TX_Size     = 7;
        HAL_UART_Transmit_DMA(&huart1, Uart1SendDataType.TxBuffer, Uart1SendDataType.TX_Size);
    }
    if ((!Comm1TaskFlag) && FlowRstCmdFlag)
    {
        Comm1TaskFlag                 = 1;
        FlowRstCmdFlag                = 0;
        Uart1SendDataType.TxBuffer[0] = PREAMBLE;
        Uart1SendDataType.TxBuffer[1] = PREAMBLE;
        Uart1SendDataType.TxBuffer[2] = STARTCMD;
        Uart1SendDataType.TxBuffer[3] = FLWRstCmd;
        Uart1SendDataType.TxBuffer[4] = FLWRstCmdPra;
        checkbuffer[0]                = FLWRstCmd;
        checkbuffer[1]                = FLWRstCmdPra;
        Uart1SendDataType.TxBuffer[5] = GetCheckSum(checkbuffer, 2);
        Uart1SendDataType.TxBuffer[6] = EOFbyte;
        Uart1SendDataType.TX_Size     = 7;
        HAL_UART_Transmit_DMA(&huart1, Uart1SendDataType.TxBuffer, Uart1SendDataType.TX_Size);
    }
    if ((!Comm1TaskFlag) && FlowPassiveReadCmdEnable)
    {
        FlowPassiveReadCmdFlag        = 1;
        Comm1TaskFlag                 = 1;
        FlowPassiveReadCmdEnable      = 0;
        Uart1SendDataType.TxBuffer[0] = PREAMBLE;
        Uart1SendDataType.TxBuffer[1] = PREAMBLE;
        Uart1SendDataType.TxBuffer[2] = STARTCMD;
        Uart1SendDataType.TxBuffer[3] = FLWSetReadCmd;
        Uart1SendDataType.TxBuffer[4] = FLWSetPassiveReadPra;
        checkbuffer[0]                = FLWSetReadCmd;
        checkbuffer[1]                = FLWSetPassiveReadPra;
        Uart1SendDataType.TxBuffer[5] = GetCheckSum(checkbuffer, 2);
        Uart1SendDataType.TxBuffer[6] = EOFbyte;
        Uart1SendDataType.TX_Size     = 7;
        HAL_UART_Transmit_DMA(&huart1, Uart1SendDataType.TxBuffer, Uart1SendDataType.TX_Size);
    }
    if ((!Comm1TaskFlag) && FlowActiveReadCmdEnable)
    {
        Comm1TaskFlag                 = 1;
        FlowPassiveReadCmdFlag        = 0;
        FlowActiveReadCmdEnable       = 0;
        Uart1SendDataType.TxBuffer[0] = PREAMBLE;
        Uart1SendDataType.TxBuffer[1] = PREAMBLE;
        Uart1SendDataType.TxBuffer[2] = STARTCMD;
        Uart1SendDataType.TxBuffer[3] = FLWSetReadCmd;
        Uart1SendDataType.TxBuffer[4] = FLWSetActiveReadPra;
        checkbuffer[0]                = FLWSetReadCmd;
        checkbuffer[1]                = FLWSetActiveReadPra;
        Uart1SendDataType.TxBuffer[5] = GetCheckSum(checkbuffer, 2);
        Uart1SendDataType.TxBuffer[6] = EOFbyte;
        Uart1SendDataType.TX_Size     = 7;
        HAL_UART_Transmit_DMA(&huart1, Uart1SendDataType.TxBuffer, Uart1SendDataType.TX_Size);
    }
    if (Timer3Uart1TimeBase10ms >= 50)
    {
        Timer3Uart1TimeBase10ms = 0;
        ModuleRecTimes++;
        if (ModuleRecTimes > 5)
            ModuleState = 1;
        else
            ModuleState = 0;
        if ((!Comm1TaskFlag) && FlowPassiveReadCmdFlag)
        {
            Comm1TaskFlag                 = 1;
            Uart1SendDataType.TxBuffer[0] = PREAMBLE;
            Uart1SendDataType.TxBuffer[1] = PREAMBLE;
            Uart1SendDataType.TxBuffer[2] = STARTCMD;
            Uart1SendDataType.TxBuffer[3] = FLWStartReadCmd;
            Uart1SendDataType.TxBuffer[4] = FLWStartReadShortPra;
            checkbuffer[0]                = FLWStartReadCmd;
            checkbuffer[1]                = FLWStartReadShortPra;
            Uart1SendDataType.TxBuffer[5] = GetCheckSum(checkbuffer, 2);
            Uart1SendDataType.TxBuffer[6] = EOFbyte;
            Uart1SendDataType.TX_Size     = 7;
            HAL_UART_Transmit_DMA(&huart1, Uart1SendDataType.TxBuffer, Uart1SendDataType.TX_Size);
        }
    }
}

/*****************************************************************************
 * 函 数 名  : Uart1_Receive_Function
 * 函数功能  : 串口1接收数据处理函数
 * 输入参数  : Uart4RxBuffer[]  数据长度:Uart1RxCounter
 * 返 回 值  :
 * 其    它  :
 *****************************************************************************/

void Uart1_Receive_Function(void)
{
    uint8_t  checksum;
    uint32_t flowrate;
    uint32_t flowtem;
    uint8_t  bcdBuf[10];

    if (Uart1HaveData == 1) // 接收完成标志=1处理，否则退出
    {
        /* BCD 协议帧最小长度: 帧头(2) + 数据(25) + 校验(1) = 28 字节 */
        if (Uart1RxCounter < 28) { Uart1RxCounter = 0; Uart1HaveData = 0; return; }
        checksum = GetCheckSum(Uart1RxBuffer, Uart1RxCounter - 2);
        if ((checksum == Uart1RxBuffer[Uart1RxCounter - 2]) && (Uart1RxBuffer[Uart1RxCounter - 1] == EOFbyte))
        {
            if ((Uart1RxBuffer[0] == 0x3c) && (Uart1RxBuffer[1] == 0x32))
            {
                ModuleRecTimes = 0;
                Cumulativeflow = ((uint64_t)Uart1RxBuffer[14] << 40) + ((uint64_t)Uart1RxBuffer[13] << 32) + ((uint64_t)Uart1RxBuffer[12] << 24) +
                                 ((uint64_t)Uart1RxBuffer[11] << 16) + ((uint64_t)Uart1RxBuffer[10] << 8) + ((uint64_t)Uart1RxBuffer[9] << 0);
                Cumulativeflow = BCD_TO_LongInt(Cumulativeflow);
                if (Uart1RxBuffer[8] == 0x0a)
                    Sumunit = 0;
                if (Uart1RxBuffer[8] == 0x1a)
                    Sumunit = 1;
                bcdBuf[0] = Uart1RxBuffer[14];
                bcdBuf[1] = Uart1RxBuffer[13];
                bcdBuf[2] = Uart1RxBuffer[12];
                bcdBuf[3] = Uart1RxBuffer[11];
                bcdBuf[4] = Uart1RxBuffer[10];
                bcdBuf[5] = Uart1RxBuffer[9];
                BCDtoStr(strFlowSumBuf, bcdBuf, 6);
                insert_char(strFlowSumBuf, '.', 9);

                flowrate  = ((uint32_t)Uart1RxBuffer[19] << 24) + ((uint32_t)Uart1RxBuffer[18] << 16) + ((uint32_t)Uart1RxBuffer[17] << 8) + Uart1RxBuffer[16];
                bcdBuf[0] = Uart1RxBuffer[19];
                bcdBuf[1] = Uart1RxBuffer[18];
                bcdBuf[2] = Uart1RxBuffer[17];
                bcdBuf[3] = Uart1RxBuffer[16];

                if (Uart1RxBuffer[15] == 0x1b)
                {
                    BCDtoStr(strFlowRateBuf, bcdBuf, 4);
                    bcdBuf[0] = Uart1RxBuffer[16];
                    BCDtoStr(strFlowRate_2Buf, bcdBuf, 1);
                    insert_char(strFlowRate_2Buf, '.', 0);
                    FlowRateValue.num = BCDTOInt(flowrate);
                    flow_filter_feed(FlowRateValue.num);
                }
                if (Uart1RxBuffer[15] == 0x0b)
                {
                    BCDtoStr(strFlowRateBuf, bcdBuf, 3);
                    bcdBuf[0] = Uart1RxBuffer[16];
                    BCDtoStr(strFlowRate_2Buf, bcdBuf, 1);
                    insert_char(strFlowRate_2Buf, '.', 0);
                    FlowRateValue.num = BCDTOInt(flowrate) / 100.0f;
                    flow_filter_feed(FlowRateValue.num);
                }

                if (Uart1RxBuffer[24] == 0x0d)
                {
                    flowtem             = ((uint32_t)Uart1RxBuffer[27] << 16) + ((uint32_t)Uart1RxBuffer[26] << 8) + ((uint32_t)Uart1RxBuffer[25] << 0);
                    FlowTemperature.num = BCDTOInt(flowtem) / 100;
                    // bcdBuf[0]           = Uart1RxBuffer[27];
                    bcdBuf[0]           = Uart1RxBuffer[26];
                    bcdBuf[1]           = Uart1RxBuffer[25];
                    BCDtoStr(strFlowTemBuf, bcdBuf, 2);
                    insert_char(strFlowTemBuf, '.', 2);
                }
                bcdBuf[0] = 0x20;
                bcdBuf[1] = 0x00;
                bcdBuf[2] = 0x00;
                BCDtoStr(strFlowPressBuf, bcdBuf, 3);
                insert_char(strFlowPressBuf, '.', 4);
            }
        }

        Uart1RxCounter = 0;
        Uart1HaveData  = 0;
    }
}

/***************************************
函数名称：crc16校验
函数功能：crc16校验
函数输入：字节指针*ptr，数据长度len
函数返回：双字节crc
函数编写：lyt
编写日期：2008年11月9日
函数版本：v0.2
****************************************/
uint16_t getCRC16(uint8_t *ptr, uint8_t len)
{
    unsigned char  i;
    unsigned short crc = 0xFFFF;
    if (len == 0)
    {
        len = 1;
    }
    while (len--)
    {
        crc ^= *ptr;
        for (i = 0; i < 8; i++)
        {
            if (crc & 1)
            {
                crc >>= 1;
                crc  ^= 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
        ptr++;
    }
    return (crc);
}
/**
 * @Author: liyongtai
 * @description:uart2 处理收发DP数据,收DP数据与PLC同步，DP数据变化串口2接收，发送定时20ms
 * @return {*}
 */
void Uart2_Communication(void)
{
    uint16_t crcresult;
    uint8_t  temp[2];

    /* 检查并应用延迟的波特率变更 (确保上次响应已在旧波特率下发送完成) */
    bsp_usart2_check_baud_rate_pending();

    if (Uart2HaveData == 1)                     // 接收完成标志=1处理，否则号?号
    {
        /* Modbus RTU 最小帧: 地址(1)+功能码(1)+数据(4)+CRC(2) = 8 */
        if (Uart2RxCounter < 8) { Uart2RxCounter = 0; Uart2HaveData = 0; return; }
        if (Uart2RxBuffer[0] == s_modbus_addr) // 地址错误不应号
        {
            crcresult = getCRC16(Uart2RxBuffer, Uart2RxCounter - 2);
            temp[1]   = crcresult & 0xff;
            temp[0]   = (crcresult >> 8) & 0xff;
            if ((Uart2RxBuffer[Uart2RxCounter - 1] == temp[0]) && (Uart2RxBuffer[Uart2RxCounter - 2] == temp[1])) // crc校验错误不应号
            {
                switch (Uart2RxBuffer[1])
                {
                    case 0x01:
                    {
                        Modbus_Function_1();
                        Uart2HaveData = 0;
                    }
                    break;
                    case 0x03:

                    {
                        Modbus_Function_3();
                        Uart2HaveData = 0;
                    }
                    break;
                    case 0x04:

                    {
                        Modbus_Function_4();
                        Uart2HaveData = 0;
                    }
                    break;
                    case 0x05:
                    {
                        Modbus_Function_5();
                        Uart2HaveData = 0;
                    }
                    break;
                    case 0x06:
                    {
                        Modbus_Function_6();
                        Uart2HaveData = 0;
                    }
                    break;
                    case 0x10:
                    {
                        Modbus_Function_10();
                        Uart2HaveData = 0;
                    }
                    break;
                }
                // CommEnbaled = 1; // 接收到触摸屏发的命令
            }
        }
        Uart2RxCounter = 0;
        Uart2HaveData  = 0;
    }
}
/*对应MODBUS 01命令函数*/
void Modbus_Function_1(void)
{
    uint16_t startaddress = 0;
    uint16_t MbBufferLen;
    uint8_t  remainder;
    uint8_t  quotient;
    uint16_t sendbytelength;
    uint16_t crcresult_1;
    uint8_t  i;
    Uart2SendDataType.TxBuffer[0] = Uart2RxBuffer[0];
    Uart2SendDataType.TxBuffer[1] = 0x01;
    startaddress                  = (((uint16_t)Uart2RxBuffer[2] << 8) + Uart2RxBuffer[3]);
    MbBufferLen                   = (((uint16_t)Uart2RxBuffer[4] << 8) + Uart2RxBuffer[5]);
    remainder                     = startaddress % 16;
    quotient                      = startaddress / 16;
    if (MbBufferLen % 8)
        sendbytelength = MbBufferLen / 8 + 1;
    else
        sendbytelength = MbBufferLen / 8;
    if ((startaddress + MbBufferLen) < (BitBufLength * 16) &&
        (quotient + 1) < BitBufLength)
    {
        /* 从 BitControlBuf[quotient] 的第 remainder 位开始，连续提取 sendbytelength 字节
         * 每次合并两个相邻寄存器得到 16 位窗口，低字节先发送 (Modbus 线圈顺序) */
        uint8_t bpos = 3;
        for (i = 0; i < sendbytelength; )
        {
            uint16_t w = (uint16_t)(((uint32_t)SWAPWORD(BitControlBuf[quotient]) >> remainder) |
                                    ((uint32_t)SWAPWORD(BitControlBuf[quotient + 1]) << (16 - remainder)));
            Uart2SendDataType.TxBuffer[bpos++] = (uint8_t)(w & 0xFF);
            i++;
            if (i >= sendbytelength) break;
            Uart2SendDataType.TxBuffer[bpos++] = (uint8_t)(w >> 8);
            i++;
            quotient++;
        }
    }
    Uart2SendDataType.TxBuffer[2]                             = sendbytelength;
    Uart2SendDataType.TX_Size                                 = sendbytelength + 3;
    crcresult_1                                               = getCRC16(Uart2SendDataType.TxBuffer, Uart2SendDataType.TX_Size);
    Uart2SendDataType.TxBuffer[Uart2SendDataType.TX_Size]     = crcresult_1 & 0xff;
    Uart2SendDataType.TxBuffer[Uart2SendDataType.TX_Size + 1] = (crcresult_1 >> 8) & 0xff;
    Uart2SendDataType.TX_Size                                 = Uart2SendDataType.TX_Size + 2;
    HAL_GPIO_WritePin(GPIOA, USART2_DE_Pin, GPIO_PIN_SET);
    Time_Delay(20);
    HAL_UART_Transmit_DMA(&huart2, Uart2SendDataType.TxBuffer, Uart2SendDataType.TX_Size);
    Uart2SendDataType.TX_Size = 0;
    Uart2RxCounter            = 0;
}
/*对应MODBUS 05命令函数*/
void Modbus_Function_5(void)
{
    uint16_t tempdress            = 0;
    // uint16_t crcresult;
    tempdress                     = ((uint16_t)Uart2RxBuffer[2] << 8) + Uart2RxBuffer[3];
    Uart2SendDataType.TxBuffer[0] = Uart2RxBuffer[0];
    Uart2SendDataType.TxBuffer[1] = 0x05;
    Uart2SendDataType.TxBuffer[2] = Uart2RxBuffer[2];
    Uart2SendDataType.TxBuffer[3] = Uart2RxBuffer[3];
    Uart2SendDataType.TxBuffer[4] = Uart2RxBuffer[4];
    Uart2SendDataType.TxBuffer[5] = Uart2RxBuffer[5];
    Uart2SendDataType.TxBuffer[6] = Uart2RxBuffer[6];
    Uart2SendDataType.TxBuffer[7] = Uart2RxBuffer[7];
    switch (tempdress)
    {
        case 0:
        {
            if (Uart2RxBuffer[4] == 0xff)
            {
                DacOutput_ENABLED;
                CalEnabledFlag = 1;
            }
            if (Uart2RxBuffer[4] == 0x0)
            {
                DacOutput_DISABLED;
                CalEnabledFlag = 0;
            }
        }
        break;
        case 1:
        {
            if (Uart2RxBuffer[4] == 0xff)
            {
                DacOutputFull_DISABLED;
                DacOutputZero_ENABLED;
                ForceDacOutFlag = 1;
                DacValue        = DacZeroValue;
            }
            if (Uart2RxBuffer[4] == 0x0)
            {
                ForceDacOutFlag = 0;
                DacOutputZero_DISABLED;
            }
        }
        break;

        case 2:
        {
            if (Uart2RxBuffer[4] == 0xff)
            {
                DacOutputZero_DISABLED;
                ForceDacOutFlag = 1;
                DacOutputFull_ENABLED;
                DacValue = DacFullValue;
            }
            if (Uart2RxBuffer[4] == 0x0)
            {
                ForceDacOutFlag = 0;
                DacOutputFull_DISABLED;
            }
        }
        break;
        case 3:
        {
            if (Uart2RxBuffer[4] == 0xff)
            {
                FlowClear_ENABLED;
                FlowClearCmdFlag = 1;
            }
            if (Uart2RxBuffer[4] == 0x0)
            {
                FlowClear_DISABLED;
            }
        }
        break;
        case 4:
        {
            if (Uart2RxBuffer[4] == 0xff)
            {
                FlowRstCmdFlag = 1;
                FlowRst_ENABLED;
            }
            if (Uart2RxBuffer[4] == 0x0)
            {
                FlowRst_DISABLED;
            }
        }
        break;
        case 5:
        {
            if (Uart2RxBuffer[4] == 0xff)
            {
                FlowActiveReadCmdEnable  = 0;
                FlowPassiveReadCmdEnable = 1;
                FlowPassiveRd_ENABLED;
            }
            if (Uart2RxBuffer[4] == 0x0)
            {
                FlowActiveReadCmdEnable  = 1;
                FlowPassiveReadCmdEnable = 0;
                FlowPassiveRd_DISABLED;
            }
        }
        break;
    }
    HAL_GPIO_WritePin(GPIOA, USART2_DE_Pin, GPIO_PIN_SET);
    Time_Delay(20);
    HAL_UART_Transmit_DMA(&huart2, Uart2SendDataType.TxBuffer, 8);
    Uart2SendDataType.TX_Size = 0;
    Uart2RxCounter            = 0;
}
/*对应MODBUS 06命令函数*/
/*对应MODBUS 06命令函数 — 写单个保持寄存器 (模拟参数)*/
void Modbus_Function_6(void)
{
    uint16_t reg_addr = ((uint16_t)Uart2RxBuffer[2] << 8) + Uart2RxBuffer[3];
    uint16_t crc_result;

    /* 写入模拟参数 */
    switch (reg_addr)
    {
        case SimSwitchAddress:  /* 48: 模拟总开关 */
            s_sim_switch = ((uint16_t)Uart2RxBuffer[4] << 8) + Uart2RxBuffer[5];
            if (sim_is_active()) sim_format_cumulative(s_sim_cumulative.num);
            break;
        case SimFlowRateAddress:     /* 50: 模拟瞬时流量 低位字 */
            s_sim_flow_rate.str[0] = Uart2RxBuffer[5];
            s_sim_flow_rate.str[1] = Uart2RxBuffer[4];
            break;
        case SimFlowRateAddress + 1: /* 51: 模拟瞬时流量 高位字 */
            s_sim_flow_rate.str[2] = Uart2RxBuffer[5];
            s_sim_flow_rate.str[3] = Uart2RxBuffer[4];
            break;
        case SimTemperatureAddress:     /* 52: 模拟温度 低位字 */
            s_sim_temperature.str[0] = Uart2RxBuffer[5];
            s_sim_temperature.str[1] = Uart2RxBuffer[4];
            break;
        case SimTemperatureAddress + 1: /* 53: 模拟温度 高位字 */
            s_sim_temperature.str[2] = Uart2RxBuffer[5];
            s_sim_temperature.str[3] = Uart2RxBuffer[4];
            break;
        case SimCumulativeAddress:     /* 54: 模拟累积流量 低位字 */
            s_sim_cumulative.str[0] = Uart2RxBuffer[5];
            s_sim_cumulative.str[1] = Uart2RxBuffer[4];
            break;
        case SimCumulativeAddress + 1: /* 55: 模拟累积流量 高位字 */
            s_sim_cumulative.str[2] = Uart2RxBuffer[5];
            s_sim_cumulative.str[3] = Uart2RxBuffer[4];
            if (sim_is_active()) sim_format_cumulative(s_sim_cumulative.num);
            break;
        default:
            break;
        /* 运行参数 (uint16 枚举) */
        case FlowUnitAddress:
            param_set_flow_unit((uint8_t)(((uint16_t)Uart2RxBuffer[4] << 8) + Uart2RxBuffer[5]));
            break;
        case TotalUnitAddress:
            param_set_total_unit((uint8_t)(((uint16_t)Uart2RxBuffer[4] << 8) + Uart2RxBuffer[5]));
            break;
        /* 运行参数 (float — 分次写入，第二半提交) */
        case MeterCoeffAddress:
            s_meter_coeff_buf.str[0] = Uart2RxBuffer[5];
            s_meter_coeff_buf.str[1] = Uart2RxBuffer[4];
            break;
        case MeterCoeffAddress + 1:
            s_meter_coeff_buf.str[2] = Uart2RxBuffer[5];
            s_meter_coeff_buf.str[3] = Uart2RxBuffer[4];
            param_set_meter_coeff(s_meter_coeff_buf.num);
            break;
        case MediumCoeffAddress:
            s_medium_coeff_buf.str[0] = Uart2RxBuffer[5];
            s_medium_coeff_buf.str[1] = Uart2RxBuffer[4];
            break;
        case MediumCoeffAddress + 1:
            s_medium_coeff_buf.str[2] = Uart2RxBuffer[5];
            s_medium_coeff_buf.str[3] = Uart2RxBuffer[4];
            param_set_medium_coeff(s_medium_coeff_buf.num);
            break;
        case SmallSignalAddress:
            s_small_signal_buf.str[0] = Uart2RxBuffer[5];
            s_small_signal_buf.str[1] = Uart2RxBuffer[4];
            break;
        case SmallSignalAddress + 1:
            s_small_signal_buf.str[2] = Uart2RxBuffer[5];
            s_small_signal_buf.str[3] = Uart2RxBuffer[4];
            param_set_small_signal(s_small_signal_buf.num);
            break;

        /* ---- 扩展参数: uint16 枚举 (单次提交) ---- */
        case StdCondAddr:
            param_set_std_cond((uint8_t)(((uint16_t)Uart2RxBuffer[4] << 8) + Uart2RxBuffer[5]));
            break;
        case PulseEquivAddr:
            param_set_pulse_equiv((uint8_t)(((uint16_t)Uart2RxBuffer[4] << 8) + Uart2RxBuffer[5]));
            break;
        case LanguageReg:
            param_set_language((uint8_t)(((uint16_t)Uart2RxBuffer[4] << 8) + Uart2RxBuffer[5]));
            break;

        /* ---- 扩展参数: float (分次写入，第二半提交) ---- */
        case FilterTimeAddr:
            s_filter_time_buf.str[0] = Uart2RxBuffer[5];
            s_filter_time_buf.str[1] = Uart2RxBuffer[4];
            break;
        case FilterTimeAddr + 1:
            s_filter_time_buf.str[2] = Uart2RxBuffer[5];
            s_filter_time_buf.str[3] = Uart2RxBuffer[4];
            param_set_filter_time(s_filter_time_buf.num);
            break;
        case DampingTimeAddr:
            s_damping_time_buf.str[0] = Uart2RxBuffer[5];
            s_damping_time_buf.str[1] = Uart2RxBuffer[4];
            break;
        case DampingTimeAddr + 1:
            s_damping_time_buf.str[2] = Uart2RxBuffer[5];
            s_damping_time_buf.str[3] = Uart2RxBuffer[4];
            param_set_damping_time(s_damping_time_buf.num);
            break;
        case FreqOutputAddr:
            s_freq_output_buf.str[0] = Uart2RxBuffer[5];
            s_freq_output_buf.str[1] = Uart2RxBuffer[4];
            break;
        case FreqOutputAddr + 1:
            s_freq_output_buf.str[2] = Uart2RxBuffer[5];
            s_freq_output_buf.str[3] = Uart2RxBuffer[4];
            param_set_freq_output(s_freq_output_buf.num);
            break;
        case DensityAddr:
            s_density_buf.str[0] = Uart2RxBuffer[5];
            s_density_buf.str[1] = Uart2RxBuffer[4];
            break;
        case DensityAddr + 1:
            s_density_buf.str[2] = Uart2RxBuffer[5];
            s_density_buf.str[3] = Uart2RxBuffer[4];
            param_set_medium_density(s_density_buf.num);
            break;
        case PipeDiaAddr:
            s_pipe_dia_buf.str[0] = Uart2RxBuffer[5];
            s_pipe_dia_buf.str[1] = Uart2RxBuffer[4];
            break;
        case PipeDiaAddr + 1:
            s_pipe_dia_buf.str[2] = Uart2RxBuffer[5];
            s_pipe_dia_buf.str[3] = Uart2RxBuffer[4];
            param_set_pipe_diameter(s_pipe_dia_buf.num);
            break;
        case GasPressAddr:
            s_gas_press_buf.str[0] = Uart2RxBuffer[5];
            s_gas_press_buf.str[1] = Uart2RxBuffer[4];
            break;
        case GasPressAddr + 1:
            s_gas_press_buf.str[2] = Uart2RxBuffer[5];
            s_gas_press_buf.str[3] = Uart2RxBuffer[4];
            param_set_gas_ref_press(s_gas_press_buf.num);
            break;
        case GasTempAddr:
            s_gas_temp_buf.str[0] = Uart2RxBuffer[5];
            s_gas_temp_buf.str[1] = Uart2RxBuffer[4];
            break;
        case GasTempAddr + 1:
            s_gas_temp_buf.str[2] = Uart2RxBuffer[5];
            s_gas_temp_buf.str[3] = Uart2RxBuffer[4];
            param_set_gas_ref_temp(s_gas_temp_buf.num);
            break;
        case ReynoldsAddr:
            s_reynolds_buf.str[0] = Uart2RxBuffer[5];
            s_reynolds_buf.str[1] = Uart2RxBuffer[4];
            break;
        case ReynoldsAddr + 1:
            s_reynolds_buf.str[2] = Uart2RxBuffer[5];
            s_reynolds_buf.str[3] = Uart2RxBuffer[4];
            param_set_reynolds_k(s_reynolds_buf.num);
            break;
        case TotalFactorAddr:
            s_total_factor_buf.str[0] = Uart2RxBuffer[5];
            s_total_factor_buf.str[1] = Uart2RxBuffer[4];
            break;
        case TotalFactorAddr + 1:
            s_total_factor_buf.str[2] = Uart2RxBuffer[5];
            s_total_factor_buf.str[3] = Uart2RxBuffer[4];
            param_set_total_factor(s_total_factor_buf.num);
            break;
        case PresetTotalAddr:
            s_preset_total_buf.str[0] = Uart2RxBuffer[5];
            s_preset_total_buf.str[1] = Uart2RxBuffer[4];
            break;
        case PresetTotalAddr + 1:
            s_preset_total_buf.str[2] = Uart2RxBuffer[5];
            s_preset_total_buf.str[3] = Uart2RxBuffer[4];
            param_set_preset_total(s_preset_total_buf.num);
            break;

        /* ---- 扩展参数: 通信地址 (立即生效) ---- */
        case CommAddrReg:
            bsp_usart_set_modbus_addr(((uint16_t)Uart2RxBuffer[4] << 8) + Uart2RxBuffer[5]);
            break;

        /* ---- 扩展参数: UART 配置 (延迟生效, 响应发送后再切换) ---- */
        case BaudRateReg:
        {
            uint8_t new_cfg = (uint8_t)(((uint16_t)Uart2RxBuffer[4] << 8) + Uart2RxBuffer[5]);
            if (param_set_uart_config(new_cfg) == HAL_OK) {
                s_uart_cfg_pending = new_cfg;
                s_baud_rate_pending = 1;
            }
            break;
        }

        /* ---- 扩展参数: OLED 自愈重初始化间隔 (uint16, 单位 100ms, 立即生效) ---- */
        case OledRecoveryAddr:
            param_set_oled_recovery_interval(
                ((uint16_t)Uart2RxBuffer[4] << 8) + Uart2RxBuffer[5]);
            break;

        /* ---- 七点标定: 标定使能 (uint16, 立即生效) ---- */
        case CalEnableAddr:
            param_set_cal_enabled((uint8_t)(((uint16_t)Uart2RxBuffer[4] << 8) + Uart2RxBuffer[5]));
            break;
    }

    /* 七点标定: k[0..6] 和 pct[0..6] (float, 分次写入, 范围判断) */
    if (reg_addr >= CalK0Addr && reg_addr <= CalK6Addr + 1) {
        uint8_t idx = (uint8_t)((reg_addr - CalK0Addr) / 2);
        if ((reg_addr - CalK0Addr) % 2 == 0) {
            s_cal_k_buf[idx].str[0] = Uart2RxBuffer[5];
            s_cal_k_buf[idx].str[1] = Uart2RxBuffer[4];
        } else {
            s_cal_k_buf[idx].str[2] = Uart2RxBuffer[5];
            s_cal_k_buf[idx].str[3] = Uart2RxBuffer[4];
            param_set_cal_k(idx, s_cal_k_buf[idx].num);
        }
    } else if (reg_addr >= CalPct0Addr && reg_addr <= CalPct6Addr + 1) {
        uint8_t idx = (uint8_t)((reg_addr - CalPct0Addr) / 2);
        if ((reg_addr - CalPct0Addr) % 2 == 0) {
            s_cal_pct_buf[idx].str[0] = Uart2RxBuffer[5];
            s_cal_pct_buf[idx].str[1] = Uart2RxBuffer[4];
        } else {
            s_cal_pct_buf[idx].str[2] = Uart2RxBuffer[5];
            s_cal_pct_buf[idx].str[3] = Uart2RxBuffer[4];
            param_set_cal_pct(idx, s_cal_pct_buf[idx].num);
        }
    }

    /* FC06 标准响应: 回显请求帧 (地址字节使用接收帧原地址，避免通信地址变更后响应地址不匹配) */
    Uart2SendDataType.TxBuffer[0] = Uart2RxBuffer[0];
    Uart2SendDataType.TxBuffer[1] = 0x06;
    Uart2SendDataType.TxBuffer[2] = Uart2RxBuffer[2];
    Uart2SendDataType.TxBuffer[3] = Uart2RxBuffer[3];
    Uart2SendDataType.TxBuffer[4] = Uart2RxBuffer[4];
    Uart2SendDataType.TxBuffer[5] = Uart2RxBuffer[5];
    Uart2SendDataType.TX_Size     = 6;
    crc_result = getCRC16(Uart2SendDataType.TxBuffer, 6);
    Uart2SendDataType.TxBuffer[6] = crc_result & 0xff;
    Uart2SendDataType.TxBuffer[7] = (crc_result >> 8) & 0xff;
    Uart2SendDataType.TX_Size     = 8;

    HAL_GPIO_WritePin(GPIOA, USART2_DE_Pin, GPIO_PIN_SET);
    Time_Delay(20);
    HAL_UART_Transmit_DMA(&huart2, Uart2SendDataType.TxBuffer, 8);
    Uart2SendDataType.TX_Size = 0;
    Uart2RxCounter            = 0;
}
/*对应MODBUS 03命令函数*/
void Modbus_Function_3(void)
{
    uint8_t  j;
    uint16_t MbBufferLen;
    uint16_t startaddress = 0;
    uint8_t  i            = 3;
    uint16_t crcresult_3;
    startaddress                  = ((uint16_t)Uart2RxBuffer[2] << 8) + Uart2RxBuffer[3];
    MbBufferLen                   = ((uint16_t)Uart2RxBuffer[4] << 8) + Uart2RxBuffer[5];
    /* Modbus 缓冲区溢出防护: TX_Size = 2*Len + 3 + 2(CRC) <= UART_TX_LEN(150) */
    if (MbBufferLen > 62) MbBufferLen = 62;
    Uart2SendDataType.TxBuffer[0] = Uart2RxBuffer[0];
    Uart2SendDataType.TxBuffer[1] = 0x03;
    Uart2SendDataType.TxBuffer[2] = 2 * MbBufferLen;
    Uart2SendDataType.TX_Size     = 2 * MbBufferLen + 3;

    if (startaddress == CumulativeflowAddress)
    {
        Uart2SendDataType.TxBuffer[i] = Cumulativeflow >> 40;
        i++;
        Uart2SendDataType.TxBuffer[i] = Cumulativeflow >> 32;
        i++;
        Uart2SendDataType.TxBuffer[i] = Cumulativeflow >> 56;
        i++;
        Uart2SendDataType.TxBuffer[i] = Cumulativeflow >> 48;
        i++;
        Uart2SendDataType.TxBuffer[i] = Cumulativeflow >> 8;
        i++;
        Uart2SendDataType.TxBuffer[i] = Cumulativeflow >> 0;
        i++;
        Uart2SendDataType.TxBuffer[i] = Cumulativeflow >> 24;
        i++;
        Uart2SendDataType.TxBuffer[i] = Cumulativeflow >> 16;
    }
    if (startaddress <= InputBufferStartMaxAddress) // flowmeter display
    {
        if (MbBufferLen <= (InputBufferStartMaxAddress - startaddress + 2))
        {
            for (j = 0; j < MbBufferLen / 2; j++) // 显示数据以连续形式放数据，数据为flowmeter
            {
                Uart2SendDataType.TxBuffer[i] = InputBuffer[j + (startaddress - InputBufferStartMinAddress) / 2].str[1];
                i++;
                Uart2SendDataType.TxBuffer[i] = InputBuffer[j + (startaddress - InputBufferStartMinAddress) / 2].str[0];
                i++;
                Uart2SendDataType.TxBuffer[i] = InputBuffer[j + (startaddress - InputBufferStartMinAddress) / 2].str[3];
                i++;
                Uart2SendDataType.TxBuffer[i] = InputBuffer[j + (startaddress - InputBufferStartMinAddress) / 2].str[2];
                i++;
            }
        }
    }
    if ((startaddress >= DacValueStartMinAddress) && (startaddress <= DacValueStartMaxAddress))
    {
        if (MbBufferLen <= (DacValueStartMaxAddress - startaddress + 1))
        {
            for (j = 0; j < MbBufferLen; j++)
            {
                Uart2SendDataType.TxBuffer[i] = (uint8_t)(DacValueBuf[j + startaddress - DacValueStartMinAddress] >> 8);
                i++;
                Uart2SendDataType.TxBuffer[i] = (uint8_t)(DacValueBuf[j + startaddress - DacValueStartMinAddress]);
                i++;
            }
        }
    }
    if ((startaddress >= SpanValueStartMinAddress) && (startaddress <= SpanValueStartMaxAddress))
    {
        /* 从 param_storage 同步到 SpanValueBuf (本地缓存) */
        SpanLoValue = param_get_value_4ma();
        SpanHiValue = param_get_value_20ma();
        if (MbBufferLen <= (SpanValueStartMaxAddress - startaddress + 2))
        {
            for (j = 0; j < MbBufferLen / 2; j++)
            {
                Uart2SendDataType.TxBuffer[i] = (uint8_t)(SpanValueBuf[j + (startaddress - SpanValueStartMinAddress) / 2].str[1]);
                i++;
                Uart2SendDataType.TxBuffer[i] = (uint8_t)(SpanValueBuf[j + (startaddress - SpanValueStartMinAddress) / 2].str[0]);
                i++;
                Uart2SendDataType.TxBuffer[i] = (uint8_t)(SpanValueBuf[j + (startaddress - SpanValueStartMinAddress) / 2].str[3]);
                i++;
                Uart2SendDataType.TxBuffer[i] = (uint8_t)(SpanValueBuf[j + (startaddress - SpanValueStartMinAddress) / 2].str[2]);
                i++;
            }
        }
    }
    /* 模拟参数区域 (寄存器 48~55) */
    if ((startaddress >= SimSwitchAddress) && (startaddress <= SimCumulativeAddress + 1))
    {
        uint16_t j;
        for (j = 0; j < MbBufferLen; j++)
        {
            uint16_t reg_val = 0;
            switch (startaddress + j)
            {
                case SimSwitchAddress:          reg_val = s_sim_switch; break;
                case SimFlowRateAddress:        reg_val = ((uint16_t)s_sim_flow_rate.str[1] << 8) | s_sim_flow_rate.str[0]; break;
                case SimFlowRateAddress + 1:    reg_val = ((uint16_t)s_sim_flow_rate.str[3] << 8) | s_sim_flow_rate.str[2]; break;
                case SimTemperatureAddress:     reg_val = ((uint16_t)s_sim_temperature.str[1] << 8) | s_sim_temperature.str[0]; break;
                case SimTemperatureAddress + 1: reg_val = ((uint16_t)s_sim_temperature.str[3] << 8) | s_sim_temperature.str[2]; break;
                case SimCumulativeAddress:      reg_val = ((uint16_t)s_sim_cumulative.str[1] << 8) | s_sim_cumulative.str[0]; break;
                case SimCumulativeAddress + 1:  reg_val = ((uint16_t)s_sim_cumulative.str[3] << 8) | s_sim_cumulative.str[2]; break;
                default: reg_val = 0; break;
            }
            Uart2SendDataType.TxBuffer[i++] = (uint8_t)(reg_val >> 8);
            Uart2SendDataType.TxBuffer[i++] = (uint8_t)(reg_val & 0xFF);
        }
    }
    /* 运行参数区域 (寄存器 22~29) */
    if ((startaddress >= FlowUnitAddress) && (startaddress <= SmallSignalAddress + 1))
    {
        uint16_t j;
        for (j = 0; j < MbBufferLen; j++)
        {
            uint16_t reg_val = 0;
            Uart_SendfloatTypeDef u;
            u.num = 0.0f;
            switch (startaddress + j)
            {
                case FlowUnitAddress:        reg_val = (uint16_t)param_get_flow_unit(); break;
                case TotalUnitAddress:       reg_val = (uint16_t)param_get_total_unit(); break;
                case MeterCoeffAddress:      u.num = param_get_meter_coeff(); reg_val = ((uint16_t)u.str[1] << 8) | u.str[0]; break;
                case MeterCoeffAddress + 1:  u.num = param_get_meter_coeff(); reg_val = ((uint16_t)u.str[3] << 8) | u.str[2]; break;
                case MediumCoeffAddress:     u.num = param_get_medium_coeff(); reg_val = ((uint16_t)u.str[1] << 8) | u.str[0]; break;
                case MediumCoeffAddress + 1: u.num = param_get_medium_coeff(); reg_val = ((uint16_t)u.str[3] << 8) | u.str[2]; break;
                case SmallSignalAddress:     u.num = param_get_small_signal(); reg_val = ((uint16_t)u.str[1] << 8) | u.str[0]; break;
                case SmallSignalAddress + 1: u.num = param_get_small_signal(); reg_val = ((uint16_t)u.str[3] << 8) | u.str[2]; break;
                default: break;
            }
            Uart2SendDataType.TxBuffer[i++] = (uint8_t)(reg_val >> 8);
            Uart2SendDataType.TxBuffer[i++] = (uint8_t)(reg_val & 0xFF);
        }
    }
    /* 扩展参数区域 (寄存器 60~93) */
    if ((startaddress >= ExtParamStartAddr) && (startaddress + MbBufferLen - 1 <= ExtParamEndAddr))
    {
        uint16_t j;
        for (j = 0; j < MbBufferLen; j++)
        {
            uint16_t reg_val = 0;
            Uart_SendfloatTypeDef u;
            u.num = 0.0f;
            switch (startaddress + j)
            {
                /* ---- 第一批: 只读运行数据 ---- */
                case RunStateAddr:
                    reg_val = (uint16_t)ModuleState;
                    break;
                case FwdTotalAddr:
                    u.num = param_get_forward_total();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case FwdTotalAddr + 1:
                    u.num = param_get_forward_total();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                case RevTotalAddr:
                    u.num = param_get_reverse_total();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case RevTotalAddr + 1:
                    u.num = param_get_reverse_total();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                case NetTotalAddr:
                    u.num = param_get_forward_total() - param_get_reverse_total();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case NetTotalAddr + 1:
                    u.num = param_get_forward_total() - param_get_reverse_total();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                case DacCurrentAddr:
                    u.num = compute_dac_current_mA();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case DacCurrentAddr + 1:
                    u.num = compute_dac_current_mA();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;

                /* ---- 第二批: 读写配置参数 ---- */
                case StdCondAddr:
                    reg_val = (uint16_t)param_get_std_cond();
                    break;
                case FilterTimeAddr:
                    u.num = param_get_filter_time();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case FilterTimeAddr + 1:
                    u.num = param_get_filter_time();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                case DampingTimeAddr:
                    u.num = param_get_damping_time();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case DampingTimeAddr + 1:
                    u.num = param_get_damping_time();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                case FreqOutputAddr:
                    u.num = param_get_freq_output();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case FreqOutputAddr + 1:
                    u.num = param_get_freq_output();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                case PulseEquivAddr:
                    reg_val = (uint16_t)param_get_pulse_equiv();
                    break;
                case DensityAddr:
                    u.num = param_get_medium_density();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case DensityAddr + 1:
                    u.num = param_get_medium_density();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                case PipeDiaAddr:
                    u.num = param_get_pipe_diameter();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case PipeDiaAddr + 1:
                    u.num = param_get_pipe_diameter();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                case GasPressAddr:
                    u.num = param_get_gas_ref_press();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case GasPressAddr + 1:
                    u.num = param_get_gas_ref_press();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                case GasTempAddr:
                    u.num = param_get_gas_ref_temp();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case GasTempAddr + 1:
                    u.num = param_get_gas_ref_temp();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                case ReynoldsAddr:
                    u.num = param_get_reynolds_k();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case ReynoldsAddr + 1:
                    u.num = param_get_reynolds_k();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;

                /* ---- 第三批: 系统参数 ---- */
                case TotalFactorAddr:
                    u.num = param_get_total_factor();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case TotalFactorAddr + 1:
                    u.num = param_get_total_factor();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                case PresetTotalAddr:
                    u.num = param_get_preset_total();
                    reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    break;
                case PresetTotalAddr + 1:
                    u.num = param_get_preset_total();
                    reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                case CommAddrReg:
                    reg_val = s_modbus_addr;
                    break;
                case BaudRateReg:
                    reg_val = (uint16_t)param_get_uart_config();
                    break;
                case LanguageReg:
                    reg_val = (uint16_t)param_get_language();
                    break;
                case OledRecoveryAddr:
                    reg_val = param_get_oled_recovery_interval();
                    break;

                /* ---- 第四批: 七点标定参数 ---- */
                case CalEnableAddr:
                    reg_val = (uint16_t)param_get_cal_enabled();
                    break;
                case CalK0Addr:     case CalK0Addr + 1:
                case CalK1Addr:     case CalK1Addr + 1:
                case CalK2Addr:     case CalK2Addr + 1:
                case CalK3Addr:     case CalK3Addr + 1:
                case CalK4Addr:     case CalK4Addr + 1:
                case CalK5Addr:     case CalK5Addr + 1:
                case CalK6Addr:     case CalK6Addr + 1:
                {
                    uint8_t idx = (uint8_t)((startaddress + j - CalK0Addr) / 2);
                    u.num = param_get_cal_k(idx);
                    if ((startaddress + j - CalK0Addr) % 2 == 0)
                        reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    else
                        reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                }
                case CalPct0Addr:    case CalPct0Addr + 1:
                case CalPct1Addr:    case CalPct1Addr + 1:
                case CalPct2Addr:    case CalPct2Addr + 1:
                case CalPct3Addr:    case CalPct3Addr + 1:
                case CalPct4Addr:    case CalPct4Addr + 1:
                case CalPct5Addr:    case CalPct5Addr + 1:
                case CalPct6Addr:    case CalPct6Addr + 1:
                {
                    uint8_t idx = (uint8_t)((startaddress + j - CalPct0Addr) / 2);
                    u.num = param_get_cal_pct(idx);
                    if ((startaddress + j - CalPct0Addr) % 2 == 0)
                        reg_val = ((uint16_t)u.str[1] << 8) | u.str[0];
                    else
                        reg_val = ((uint16_t)u.str[3] << 8) | u.str[2];
                    break;
                }

                default: break;
            }
            Uart2SendDataType.TxBuffer[i++] = (uint8_t)(reg_val >> 8);
            Uart2SendDataType.TxBuffer[i++] = (uint8_t)(reg_val & 0xFF);
        }
    }
    crcresult_3                                               = getCRC16(Uart2SendDataType.TxBuffer, Uart2SendDataType.TX_Size);
    Uart2SendDataType.TxBuffer[Uart2SendDataType.TX_Size]     = crcresult_3 & 0xff;
    Uart2SendDataType.TxBuffer[Uart2SendDataType.TX_Size + 1] = (crcresult_3 >> 8) & 0xff;
    Uart2SendDataType.TX_Size                                 = Uart2SendDataType.TX_Size + 2;
    HAL_GPIO_WritePin(GPIOA, USART2_DE_Pin, GPIO_PIN_SET);
    Time_Delay(20);
    HAL_UART_Transmit_DMA(&huart2, Uart2SendDataType.TxBuffer, Uart2SendDataType.TX_Size);
    Uart2SendDataType.TX_Size = 0;
    Uart2RxCounter            = 0;
}
/*对应MODBUS 04命令函数,对应输入寄存号*/
void Modbus_Function_4(void)
{
    uint8_t  temp;
    uint16_t tempdress = 0;
    // uint8_t  i         = 3;
    uint16_t crcresult_4;
    tempdress                     = ((uint16_t)Uart2RxBuffer[2] << 8) + Uart2RxBuffer[3];
    Uart2SendDataType.TxBuffer[0] = Uart2RxBuffer[0];
    Uart2SendDataType.TxBuffer[1] = 0x04;
    temp                          = Uart2RxBuffer[5];
    if (temp > 62) temp = 62;   /* 缓冲区溢出防护 */
    Uart2SendDataType.TxBuffer[2] = 2 * temp;
    Uart2SendDataType.TX_Size     = 2 * temp + 3;

    if (tempdress == 19)
    { /*
       for (j = 0; j < temp; j++) //
       显示数据以连续形式放数据,存放的数据为标度度变换的数据
       {
         Uart2SendDataType.TxBuffer[i] = DataBuffer[j] >> 8 &
       0xff; i++; Uart2SendDataType.TxBuffer[i] =
       DataBuffer[j] & 0xff; i++;
       }
   */
    }
    crcresult_4                                               = getCRC16(Uart2SendDataType.TxBuffer, Uart2SendDataType.TX_Size);
    Uart2SendDataType.TxBuffer[Uart2SendDataType.TX_Size]     = crcresult_4 & 0xff;
    Uart2SendDataType.TxBuffer[Uart2SendDataType.TX_Size + 1] = (crcresult_4 >> 8) & 0xff;
    Uart2SendDataType.TX_Size                                 = Uart2SendDataType.TX_Size + 2;
    HAL_GPIO_WritePin(GPIOA, USART2_DE_Pin, GPIO_PIN_SET);
    Time_Delay(20);
    HAL_UART_Transmit_DMA(&huart2, Uart2SendDataType.TxBuffer, Uart2SendDataType.TX_Size);
    Uart2SendDataType.TX_Size = 0;
    Uart2RxCounter            = 0;
}
/*对应MODBUS 0x10命令函数*/
void Modbus_Function_10(void)
{
    uint32_t BackupBuf[2];
    uint8_t  i;
    uint16_t startaddress = 0;
    uint16_t MbBufferLen;
    uint16_t crcresult_10;
    startaddress = ((uint16_t)Uart2RxBuffer[2] << 8) + Uart2RxBuffer[3];
    MbBufferLen  = ((uint16_t)Uart2RxBuffer[4] << 8) + Uart2RxBuffer[5];
    if (Uart2RxBuffer[6] == Uart2RxBuffer[5] * 2)
    {

        if ((startaddress >= DacValueStartMinAddress) && (startaddress <= DacValueStartMaxAddress))
        {
            for (i = 0; i < MbBufferLen; i++)
            {
                if ((i + startaddress) > DacValueStartMaxAddress)
                    break;
                DacValueBuf[startaddress - DacValueStartMinAddress + i] =
                    ((uint16_t)Uart2RxBuffer[7 + 2 * i] << 8) + Uart2RxBuffer[7 + 2 * i + 1]; // hold register value
            }
            if (CalEnabledFlag)
            {
                if (startaddress == DacValueStartMinAddress)
                {
                    DacValue = DacZeroValue;
                }
                if (startaddress == DacValueStartMaxAddress)
                {
                    DacValue = DacFullValue;
                }
                WriteBufferFlash_16(2, DAC_FLASH_PAGE_ADDR, DacValueBuf);
            }
        }
        if ((startaddress >= SpanValueStartMinAddress) && (startaddress <= SpanValueStartMaxAddress))
        {
            for (i = 0; i < MbBufferLen / 2; i++)
            {
                if ((i + startaddress) > SpanValueStartMaxAddress)
                    break;
                SpanValueBuf[(startaddress - SpanValueStartMinAddress) / 2 + i].str[1] = Uart2RxBuffer[7 + 2 * i];
                SpanValueBuf[(startaddress - SpanValueStartMinAddress) / 2 + i].str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                SpanValueBuf[(startaddress - SpanValueStartMinAddress) / 2 + i].str[3] = Uart2RxBuffer[7 + 2 * i + 2];
                SpanValueBuf[(startaddress - SpanValueStartMinAddress) / 2 + i].str[2] = Uart2RxBuffer[7 + 2 * i + 3];
            }
            BackupBuf[0] = ((uint32_t)SpanValueBuf[0].str[0] << 24) + ((uint32_t)SpanValueBuf[0].str[1] << 16) + ((uint32_t)SpanValueBuf[0].str[2] << 8) +
                           SpanValueBuf[0].str[3];
            BackupBuf[1] = ((uint32_t)SpanValueBuf[1].str[0] << 24) + ((uint32_t)SpanValueBuf[1].str[1] << 16) + ((uint32_t)SpanValueBuf[1].str[2] << 8) +
                           SpanValueBuf[1].str[3];
            WriteBufferFlash(2, ADDR_FLASH_PAGE_63, BackupBuf);
            /* 同步到 param_storage RAM 缓存 */
            param_set_value_4ma(SpanLoValue);
            param_set_value_20ma(SpanHiValue);
        }
        /* 运行参数区域 (寄存器 22~29) */
        if ((startaddress >= FlowUnitAddress) && (startaddress <= SmallSignalAddress + 1))
        {
            for (i = 0; i < MbBufferLen; i++)
            {
                uint16_t fc10_addr = startaddress + i;
                uint16_t fc10_val  = ((uint16_t)Uart2RxBuffer[7 + 2 * i] << 8) + Uart2RxBuffer[7 + 2 * i + 1];
                switch (fc10_addr)
                {
                    case FlowUnitAddress:
                        param_set_flow_unit((uint8_t)fc10_val);
                        break;
                    case TotalUnitAddress:
                        param_set_total_unit((uint8_t)fc10_val);
                        break;
                    case MeterCoeffAddress:
                        s_meter_coeff_buf.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_meter_coeff_buf.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case MeterCoeffAddress + 1:
                        s_meter_coeff_buf.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_meter_coeff_buf.str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_meter_coeff(s_meter_coeff_buf.num);
                        break;
                    case MediumCoeffAddress:
                        s_medium_coeff_buf.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_medium_coeff_buf.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case MediumCoeffAddress + 1:
                        s_medium_coeff_buf.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_medium_coeff_buf.str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_medium_coeff(s_medium_coeff_buf.num);
                        break;
                    case SmallSignalAddress:
                        s_small_signal_buf.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_small_signal_buf.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case SmallSignalAddress + 1:
                        s_small_signal_buf.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_small_signal_buf.str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_small_signal(s_small_signal_buf.num);
                        break;
                    default: break;
                }
            }
        }
        /* 模拟参数区域 (寄存器 48~55) — FC16 Bug 修复 */
        if ((startaddress >= SimSwitchAddress) && (startaddress <= SimCumulativeAddress + 1))
        {
            for (i = 0; i < MbBufferLen; i++)
            {
                uint16_t fc10_addr = startaddress + i;
                switch (fc10_addr)
                {
                    case SimSwitchAddress:
                        s_sim_switch = ((uint16_t)Uart2RxBuffer[7 + 2 * i] << 8) + Uart2RxBuffer[7 + 2 * i + 1];
                        if (sim_is_active()) sim_format_cumulative(s_sim_cumulative.num);
                        break;
                    case SimFlowRateAddress:
                        s_sim_flow_rate.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_sim_flow_rate.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case SimFlowRateAddress + 1:
                        s_sim_flow_rate.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_sim_flow_rate.str[3] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case SimTemperatureAddress:
                        s_sim_temperature.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_sim_temperature.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case SimTemperatureAddress + 1:
                        s_sim_temperature.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_sim_temperature.str[3] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case SimCumulativeAddress:
                        s_sim_cumulative.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_sim_cumulative.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case SimCumulativeAddress + 1:
                        s_sim_cumulative.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_sim_cumulative.str[3] = Uart2RxBuffer[7 + 2 * i];
                        if (sim_is_active()) sim_format_cumulative(s_sim_cumulative.num);
                        break;
                    default: break;
                }
            }
        }
        /* 扩展配置参数区域 (寄存器 69~93) */
        if ((startaddress >= StdCondAddr) && (startaddress <= OledRecoveryAddr))
        {
            for (i = 0; i < MbBufferLen; i++)
            {
                uint16_t fc10_addr = startaddress + i;
                switch (fc10_addr)
                {
                    /* uint16 枚举 (单次提交) */
                    case StdCondAddr:
                        param_set_std_cond((uint8_t)(((uint16_t)Uart2RxBuffer[7 + 2 * i] << 8) + Uart2RxBuffer[7 + 2 * i + 1]));
                        break;
                    case PulseEquivAddr:
                        param_set_pulse_equiv((uint8_t)(((uint16_t)Uart2RxBuffer[7 + 2 * i] << 8) + Uart2RxBuffer[7 + 2 * i + 1]));
                        break;
                    case LanguageReg:
                        param_set_language((uint8_t)(((uint16_t)Uart2RxBuffer[7 + 2 * i] << 8) + Uart2RxBuffer[7 + 2 * i + 1]));
                        break;

                    /* float 参数 (低位字缓存, 高位字提交) */
                    case FilterTimeAddr:
                        s_filter_time_buf.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_filter_time_buf.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case FilterTimeAddr + 1:
                        s_filter_time_buf.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_filter_time_buf.str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_filter_time(s_filter_time_buf.num);
                        break;
                    case DampingTimeAddr:
                        s_damping_time_buf.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_damping_time_buf.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case DampingTimeAddr + 1:
                        s_damping_time_buf.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_damping_time_buf.str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_damping_time(s_damping_time_buf.num);
                        break;
                    case FreqOutputAddr:
                        s_freq_output_buf.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_freq_output_buf.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case FreqOutputAddr + 1:
                        s_freq_output_buf.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_freq_output_buf.str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_freq_output(s_freq_output_buf.num);
                        break;
                    case DensityAddr:
                        s_density_buf.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_density_buf.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case DensityAddr + 1:
                        s_density_buf.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_density_buf.str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_medium_density(s_density_buf.num);
                        break;
                    case PipeDiaAddr:
                        s_pipe_dia_buf.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_pipe_dia_buf.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case PipeDiaAddr + 1:
                        s_pipe_dia_buf.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_pipe_dia_buf.str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_pipe_diameter(s_pipe_dia_buf.num);
                        break;
                    case GasPressAddr:
                        s_gas_press_buf.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_gas_press_buf.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case GasPressAddr + 1:
                        s_gas_press_buf.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_gas_press_buf.str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_gas_ref_press(s_gas_press_buf.num);
                        break;
                    case GasTempAddr:
                        s_gas_temp_buf.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_gas_temp_buf.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case GasTempAddr + 1:
                        s_gas_temp_buf.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_gas_temp_buf.str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_gas_ref_temp(s_gas_temp_buf.num);
                        break;
                    case ReynoldsAddr:
                        s_reynolds_buf.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_reynolds_buf.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case ReynoldsAddr + 1:
                        s_reynolds_buf.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_reynolds_buf.str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_reynolds_k(s_reynolds_buf.num);
                        break;
                    case TotalFactorAddr:
                        s_total_factor_buf.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_total_factor_buf.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case TotalFactorAddr + 1:
                        s_total_factor_buf.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_total_factor_buf.str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_total_factor(s_total_factor_buf.num);
                        break;
                    case PresetTotalAddr:
                        s_preset_total_buf.str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_preset_total_buf.str[1] = Uart2RxBuffer[7 + 2 * i];
                        break;
                    case PresetTotalAddr + 1:
                        s_preset_total_buf.str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_preset_total_buf.str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_preset_total(s_preset_total_buf.num);
                        break;

                    /* 通信地址: 立即生效 */
                    case CommAddrReg:
                        bsp_usart_set_modbus_addr(((uint16_t)Uart2RxBuffer[7 + 2 * i] << 8) + Uart2RxBuffer[7 + 2 * i + 1]);
                        break;
                    /* UART 配置: 延迟生效, 响应发送后再切换 */
                    case BaudRateReg:
                    {
                        uint8_t new_cfg = (uint8_t)(((uint16_t)Uart2RxBuffer[7 + 2 * i] << 8) + Uart2RxBuffer[7 + 2 * i + 1]);
                        if (param_set_uart_config(new_cfg) == HAL_OK) {
                            s_uart_cfg_pending = new_cfg;
                            s_baud_rate_pending = 1;
                        }
                        break;
                    }

                    /* OLED 自愈重初始化间隔: 立即生效 */
                    case OledRecoveryAddr:
                        param_set_oled_recovery_interval(
                            ((uint16_t)Uart2RxBuffer[7 + 2 * i] << 8) + Uart2RxBuffer[7 + 2 * i + 1]);
                        break;

                    default: break;
                }
            }
        }
        /* 七点标定参数区域 (寄存器 95~123) — 紧凑范围判断 */
        if ((startaddress >= CalEnableAddr) && (startaddress + MbBufferLen - 1 <= CalEndAddr))
        {
            for (i = 0; i < MbBufferLen; i++)
            {
                uint16_t fc10_addr = startaddress + i;
                if (fc10_addr == CalEnableAddr) {
                    param_set_cal_enabled((uint8_t)(((uint16_t)Uart2RxBuffer[7 + 2 * i] << 8) + Uart2RxBuffer[7 + 2 * i + 1]));
                } else if (fc10_addr >= CalK0Addr && fc10_addr <= CalK6Addr + 1) {
                    uint8_t idx = (uint8_t)((fc10_addr - CalK0Addr) / 2);
                    if ((fc10_addr - CalK0Addr) % 2 == 0) {
                        s_cal_k_buf[idx].str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_cal_k_buf[idx].str[1] = Uart2RxBuffer[7 + 2 * i];
                    } else {
                        s_cal_k_buf[idx].str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_cal_k_buf[idx].str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_cal_k(idx, s_cal_k_buf[idx].num);
                    }
                } else if (fc10_addr >= CalPct0Addr && fc10_addr <= CalPct6Addr + 1) {
                    uint8_t idx = (uint8_t)((fc10_addr - CalPct0Addr) / 2);
                    if ((fc10_addr - CalPct0Addr) % 2 == 0) {
                        s_cal_pct_buf[idx].str[0] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_cal_pct_buf[idx].str[1] = Uart2RxBuffer[7 + 2 * i];
                    } else {
                        s_cal_pct_buf[idx].str[2] = Uart2RxBuffer[7 + 2 * i + 1];
                        s_cal_pct_buf[idx].str[3] = Uart2RxBuffer[7 + 2 * i];
                        param_set_cal_pct(idx, s_cal_pct_buf[idx].num);
                    }
                }
            }
        }
        Uart2SendDataType.TxBuffer[0]                             = Uart2RxBuffer[0];
        Uart2SendDataType.TxBuffer[1]                             = 0x10;
        Uart2SendDataType.TxBuffer[2]                             = Uart2RxBuffer[2];
        Uart2SendDataType.TxBuffer[3]                             = Uart2RxBuffer[3];
        Uart2SendDataType.TxBuffer[4]                             = Uart2RxBuffer[4];
        Uart2SendDataType.TxBuffer[5]                             = Uart2RxBuffer[5];
        Uart2SendDataType.TX_Size                                 = 6;
        crcresult_10                                              = getCRC16(Uart2SendDataType.TxBuffer, Uart2SendDataType.TX_Size);
        Uart2SendDataType.TxBuffer[Uart2SendDataType.TX_Size]     = crcresult_10 & 0xff;
        Uart2SendDataType.TxBuffer[Uart2SendDataType.TX_Size + 1] = (crcresult_10 >> 8) & 0xff;
        Uart2SendDataType.TX_Size                                 = Uart2SendDataType.TX_Size + 2;
        HAL_GPIO_WritePin(GPIOA, USART2_DE_Pin, GPIO_PIN_SET);
        Time_Delay(20);
        HAL_UART_Transmit_DMA(&huart2, Uart2SendDataType.TxBuffer, Uart2SendDataType.TX_Size);
        Uart2SendDataType.TX_Size = 0;
        Uart2RxCounter            = 0;
    }
}

/**
 * @brief   设置 Modbus 从站地址 (运行时)
 */
void bsp_usart_set_modbus_addr(uint16_t addr)
{
    if (addr >= 1 && addr <= 247) {
        s_modbus_addr = addr;
        param_set_modbus_addr(addr);
    }
}

/**
 * @brief   应用 UART 配置变更到 USART2 硬件
 * @param   uart_config  packed 配置值 (bit[2:0]=baud, bit[4:3]=parity, bit[5]=stop)
 * @note    DeInit → 重设 BaudRate/Parity/StopBits/WordLength → Init → 重启 DMA + IDLE
 *          仅限主循环上下文调用，禁止在 ISR 中使用
 */
void bsp_usart2_apply_uart_config(uint8_t uart_config)
{
    uint8_t baud_idx = uart_cfg_baud(uart_config);
    uint8_t parity   = uart_cfg_parity(uart_config);
    uint8_t stopbits = uart_cfg_stop(uart_config);

    if (baud_idx >= BAUD_RATE_COUNT) return;

    HAL_UART_Abort(&huart2);
    HAL_UART_DeInit(&huart2);

    huart2.Init.BaudRate = s_baud_table[baud_idx];

    /* 校验位 → WordLength 耦合: 启用校验必须 UART_WORDLENGTH_9B */
    if (parity == PARITY_NONE) {
        huart2.Init.WordLength = UART_WORDLENGTH_8B;
        huart2.Init.Parity     = UART_PARITY_NONE;
    } else if (parity == PARITY_ODD) {
        huart2.Init.WordLength = UART_WORDLENGTH_9B;
        huart2.Init.Parity     = UART_PARITY_ODD;
    } else {
        huart2.Init.WordLength = UART_WORDLENGTH_9B;
        huart2.Init.Parity     = UART_PARITY_EVEN;
    }

    huart2.Init.StopBits = (stopbits == STOPBITS_2) ? UART_STOPBITS_2 : UART_STOPBITS_1;

    HAL_UART_Init(&huart2);

    Uart2ReceiveType.RX_Flag = 0;
    Uart2ReceiveType.RX_Size = 0;
    __HAL_UART_CLEAR_IDLEFLAG(&huart2);
    EnableUart_IT_IDLE(&huart2, &Uart2ReceiveType);

    Uart2RxCounter = 0;
    Uart2HaveData  = 0;
}

/**
 * @brief   检查并应用延迟的 UART 配置变更
 * @note    在 Uart2_Communication() 入口调用，确保 Modbus 响应已在旧配置下发送完成
 */
void bsp_usart2_check_baud_rate_pending(void)
{
    if (!s_baud_rate_pending) return;

    /* 等待 DMA 发送完成 (gState == READY 表示空闲) */
    if (huart2.gState != HAL_UART_STATE_READY) return;

    uint8_t cfg = s_uart_cfg_pending;
    s_baud_rate_pending = 0;

    bsp_usart2_apply_uart_config(cfg);
}

/* ========== 模拟参数 getter 实现 ========== */

/**
 * @brief   格式化模拟累积流量为显示字符串
 * @note    与 BCDtoStr + insert_char('.','9') 格式匹配: "XXXXXXXXX.XXX"
 */
static void sim_format_cumulative(float value)
{
    if (value < 0.0f) value = 0.0f;
    uint32_t scaled    = (uint32_t)(value * 1000.0f + 0.5f);
    uint32_t int_part  = scaled / 1000;
    uint32_t frac_part = scaled % 1000;
    u32_to_str_pad(int_part, (char *)s_sim_flow_sum_buf, 9);
    s_sim_flow_sum_buf[9] = '.';
    u32_to_str_pad(frac_part, (char *)s_sim_flow_sum_buf + 10, 3);
    s_sim_flow_sum_buf[13] = '\0';
}

/**
 * @brief   计算当前 4-20mA 电流输出值
 * @note    公式: 4 + (DacValue - ZeroValue) / (FullValue - ZeroValue) * 16
 */
static float compute_dac_current_mA(void)
{
    uint16_t zero = DacZeroValue;
    uint16_t full = DacFullValue;
    float ratio;
    if (full == zero) return 4.0f;
    ratio = (float)((int32_t)DacValue - (int32_t)zero) / (float)((int32_t)full - (int32_t)zero);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    return 4.0f + ratio * 16.0f;
}

uint8_t sim_is_active(void)
{
    return (s_sim_switch != 0) ? 1 : 0;
}

float effective_flow_rate(void)
{
    if (sim_is_active()) return s_sim_flow_rate.num;
    if (s_flt_valid) return s_flt_result;
    return FlowRateValue.num;
}

float convert_flow_rate_from_lph(
    float flow_lph,
    uint8_t target_unit,
    float density_kg_m3)
{
    switch (target_unit)
    {
    case FLOW_UNIT_M3H:
        return flow_lph / 1000.0f;

    case FLOW_UNIT_LH:
        return flow_lph;

    case FLOW_UNIT_LMIN:
        return flow_lph / 60.0f;

    case FLOW_UNIT_KGH:
        return flow_lph * density_kg_m3 / 1000.0f;

    default:
        return flow_lph;
    }
}

static uint64_t mul_div_round_u64(uint64_t value, uint32_t multiplier, uint64_t divisor)
{
    uint64_t quotient;
    uint64_t remainder;
    uint64_t fraction;
    uint64_t fraction_remainder;
    uint64_t result;

    quotient = value / divisor;
    remainder = value % divisor;

    if (quotient > UINT64_MAX / multiplier)
        return UINT64_MAX;

    result = quotient * multiplier;
    remainder *= multiplier;
    fraction = remainder / divisor;
    fraction_remainder = remainder % divisor;
    if (fraction_remainder >= (divisor + 1ULL) / 2ULL)
        fraction++;
    if (result > UINT64_MAX - fraction)
        return UINT64_MAX;

    return result + fraction;
}

static uint64_t convert_total_to_milli_unit(
    uint64_t raw_total,
    uint8_t source_unit,
    uint8_t target_unit,
    float density_kg_m3)
{
    uint64_t milli_liter;
    uint32_t density_x1000;

    if (source_unit == 1)
    {
        if (raw_total > UINT64_MAX / 1000ULL)
            return UINT64_MAX;
        milli_liter = raw_total * 1000ULL;
    }
    else
    {
        milli_liter = raw_total;
    }

    switch (target_unit)
    {
    case TOTAL_UNIT_M3:
        return mul_div_round_u64(milli_liter, 1, 1000ULL);

    case TOTAL_UNIT_L:
        return milli_liter;

    case TOTAL_UNIT_KG:
        density_x1000 = (uint32_t)(density_kg_m3 * 1000.0f + 0.5f);
        return mul_div_round_u64(milli_liter, density_x1000, 1000000ULL);

    case TOTAL_UNIT_T:
        density_x1000 = (uint32_t)(density_kg_m3 * 1000.0f + 0.5f);
        return mul_div_round_u64(milli_liter, density_x1000, 1000000000ULL);

    default:
        return mul_div_round_u64(milli_liter, 1, 1000ULL);
    }
}

static void format_milli_total(
    uint64_t milli_value,
    unsigned char *p_buf,
    uint8_t buf_size)
{
    uint64_t integer_part;
    uint32_t fraction_part;

    if ((p_buf == NULL) || (buf_size < 14))
        return;

    integer_part = milli_value / 1000ULL;
    fraction_part = (uint32_t)(milli_value % 1000ULL);

    if (integer_part > 999999999ULL)
    {
        memcpy(p_buf, "-------------", 13);
        p_buf[13] = '\0';
        return;
    }

    u32_to_str_pad((uint32_t)integer_part, (char *)p_buf, 9);
    p_buf[9] = '.';
    u32_to_str_pad(fraction_part, (char *)p_buf + 10, 3);
    p_buf[13] = '\0';
}

float effective_temperature(void)
{
    return sim_is_active() ? s_sim_temperature.num : FlowTemperature.num;
}

const unsigned char *effective_flow_sum_buf(const unsigned char *real_buf)
{
    static unsigned char display_buf[20];
    const unsigned char *source_buf;
    uint64_t raw_total;
    uint64_t milli_value;
    uint8_t source_unit;
    uint8_t target_unit;

    target_unit = param_get_total_unit();

    if (sim_is_active())
    {
        raw_total = (s_sim_cumulative.num > 0.0f)
            ? (uint64_t)(s_sim_cumulative.num * 1000.0f + 0.5f)
            : 0;
        source_unit = 0;
        source_buf = s_sim_flow_sum_buf;
    }
    else
    {
        raw_total = Cumulativeflow;
        source_unit = Sumunit;
        source_buf = real_buf;
    }

    if (((source_unit == 0) && (target_unit == TOTAL_UNIT_L)) ||
        ((source_unit == 1) && (target_unit == TOTAL_UNIT_M3)))
        return source_buf;

    milli_value = convert_total_to_milli_unit(
        raw_total,
        source_unit,
        target_unit,
        param_get_medium_density());
    format_milli_total(milli_value, display_buf, sizeof(display_buf));

    return display_buf;
}

/**
 * @Author: liyongtai
 * @description: 4个字节转换为IEEE 754浮点数
 * @param {uint8_t} *raw
 * @return {*}
 */
float raw2ieee(uint8_t *raw)
{
    union
    {
        uint8_t bytes[4];
        float   fp;
    } un;
    memcpy(un.bytes, raw, 4);
    return un.fp;
    // return *(float *)raw;
}
/**
 * @Author: liyongtai
 * @description:
 * @param {uint8_t} *ptr
 * @param {uint8_t} len,限制len<256
 * @return :单字节校验和
 */
uint8_t GetCheckSum(uint8_t *ptr, uint8_t len)
{
    uint8_t  i;
    uint16_t Sum = 0;
    uint8_t  value;
    for (i = 0; i < len; i++)
        Sum = Sum + *(ptr++);
    value = Sum % 256;
    return value;
}
/**
 * @Author: liyongtai
 * @description:
 * @param {uint16_t} word
 * @return {*}
 */
uint16_t SWAPWORD(uint16_t word)
{
    uint8_t  tmp;
    uint8_t *b;
    uint16_t w;
    w    = word;
    b    = (uint8_t *)&w;
    tmp  = b[0];
    b[0] = b[1];
    b[1] = tmp;
    return w;
}
/**
 * @Author: liyongtai
 * @description:char to char
 * @param {uint8_t} bcd
 * @return {*}
 */
uint8_t BCD2DEC(uint8_t bcd)
{
    return (bcd - (bcd >> 4) * 6);
}
/**
 * @Author: liyongtai
 * @description:uint32_t bcd to int
 * @param {uint32_t} bcd
 * @return {uint32_t x}
 */
float BCDTOInt(uint32_t bcd)
{
    uint8_t  a, b, c, d;
    uint32_t x;
    a = (uint8_t)(bcd >> 24);
    b = (uint8_t)(bcd >> 16);
    c = (uint8_t)(bcd >> 8);
    d = (uint8_t)(bcd >> 0);
    x = (uint32_t)BCD2DEC(a) * 1000000 + (uint32_t)BCD2DEC(b) * 10000 + (uint32_t)BCD2DEC(c) * 100 + BCD2DEC(d);
    return x;
}
/**
 * @Author: liyongtai
 * @description:uint64_t bcd to int
 * @param {uint64_t} bcd
 * @return {uint64_t x}
 */
uint64_t BCD_TO_LongInt(uint64_t bcd)
{
    uint8_t  a, b, c, d, e, f, g, h;
    uint64_t x;
    a = (uint8_t)(bcd >> 56);
    b = (uint8_t)(bcd >> 48);
    c = (uint8_t)(bcd >> 40);
    d = (uint8_t)(bcd >> 32);
    e = (uint8_t)(bcd >> 24);
    f = (uint8_t)(bcd >> 16);
    g = (uint8_t)(bcd >> 8);
    h = (uint8_t)(bcd >> 0);
    x = (uint64_t)BCD2DEC(a) * 100000000000000 + (uint64_t)BCD2DEC(b) * 1000000000000 + (uint64_t)BCD2DEC(c) * 10000000000 + (uint64_t)BCD2DEC(d) * 100000000 +
        (uint64_t)BCD2DEC(e) * 1000000 + (uint64_t)BCD2DEC(f) * 10000 + (uint64_t)BCD2DEC(g) * 100 + (uint64_t)BCD2DEC(h);
    return x;
}

uint8_t BCDtoStr(unsigned char *str, unsigned char *BCD, int BCD_length)
{
    if (BCD == 0 || BCD_length == 0)
        return 0;
    int i, j;
    for (i = 0, j = 0; i < BCD_length; i++, j += 2)
    {
        str[j]     = (BCD[i] >> 4) > 9 ? (BCD[i] >> 4) - 10 + 'A' : (BCD[i] >> 4) + '0';
        str[j + 1] = (BCD[i] & 0x0F) > 9 ? (BCD[i] & 0x0F) - 10 + 'A' : (BCD[i] & 0x0F) + '0';
    }
    str[j] = '\0';
    return 1;
}
