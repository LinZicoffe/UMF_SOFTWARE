#include "doubles.h"

/* 使用真实 HAL 类型和地址常量，但所有边界函数都只操作下列测试状态。 */
UART_HandleTypeDef huart1 = { .Instance = USART1 };
UART_HandleTypeDef huart2 = { .Instance = USART2, .gState = HAL_UART_STATE_READY };
DMA_HandleTypeDef hdma_usart1_tx;
DMA_HandleTypeDef hdma_usart2_tx;
volatile uint16_t Timer3Uart1TimeBase10ms;
uint16_t BitControlBuf[50];

sim_config_t sim_config = {
    .sample_ms = 500, .window = 2, .tau = 0.5f,
    .range_low = 0.0f, .range_high = 100.0f,
    .k = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f },
    .pct = { 0.0f, 10.0f, 20.0f, 40.0f, 60.0f, 80.0f, 100.0f }
};
sim_event_t sim_events[SIM_EVENT_CAPACITY];
uint32_t sim_event_count;
uint32_t sim_boundary_error;
uint32_t sim_now_ms;
uint32_t sim_key_mask;
uint32_t sim_gpio_read_count;
HAL_StatusTypeDef sim_tx_status = HAL_OK;

static sim_event_t *record_event(uint32_t kind)
{
    sim_event_t *event;
    if (sim_event_count >= SIM_EVENT_CAPACITY) {
        sim_boundary_error = 1;  /* 不静默丢掉输出事件 */
        return NULL;
    }
    event = &sim_events[sim_event_count++];
    event->kind = kind;
    event->at_ms = sim_now_ms;
    return event;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    uint8_t bit = 0;
    sim_gpio_read_count++;
    if (port == K_MOV_GPIO_Port && pin == K_MOV_Pin) bit = 1;
    else if (port == K_ADD_GPIO_Port && pin == K_ADD_Pin) bit = 2;
    else if (port == K_SUB_GPIO_Port && pin == K_SUB_Pin) bit = 4;
    else sim_boundary_error = 2;
    return (sim_key_mask & bit) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState value)
{
    sim_event_t *event = record_event(SIM_GPIO_WRITE);
    if (event) {
        event->arg0 = (uint32_t)port;
        event->arg1 = pin;
        event->result = value;
    }
    if (port != USART2_DE_GPIO_Port || pin != USART2_DE_Pin) sim_boundary_error = 3;
}

void Time_Delay(uint32_t count)
{
    /* 生产 Time_Delay 的 count 是忙等待次数，不能冒充毫秒。这里只记录请求。 */
    sim_event_t *event = record_event(SIM_DELAY);
    if (event) event->arg0 = count;
}

HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *uart, const uint8_t *data, uint16_t length)
{
    sim_event_t *event = record_event(SIM_UART_TX);
    if ((uart != &huart1 && uart != &huart2) || length > UART_TX_LEN) {
        sim_boundary_error = 4;
        return HAL_ERROR;
    }
    if (event) {
        event->arg0 = (uint32_t)uart->Instance;
        event->length = length;
        event->result = sim_tx_status;
        memcpy(event->data, data, length);
    }
    if (sim_tx_status == HAL_OK) uart->gState = HAL_UART_STATE_BUSY_TX;
    return sim_tx_status;
}

/* 仅返回输入配置，绝不计算校准/滤波的期望输出。 */
uint16_t param_get_sample_interval_ms(void) { return sim_config.sample_ms; }
uint16_t param_get_filter_window_count(void) { return sim_config.window; }
float param_get_filter_time(void) { return sim_config.tau; }
uint8_t param_get_cal_enabled(void) { return sim_config.cal_enabled; }
float param_get_value_4ma(void) { return sim_config.range_low; }
float param_get_value_20ma(void) { return sim_config.range_high; }
float param_get_cal_k(uint8_t index)
{
    if (index >= CAL_POINT_COUNT) { sim_boundary_error = 5; return 0; }
    return sim_config.k[index];
}
float param_get_cal_pct(uint8_t index)
{
    if (index >= CAL_POINT_COUNT) { sim_boundary_error = 6; return 0; }
    return sim_config.pct[index];
}

void sim_advance_10ms(uint16_t ticks)
{
    sim_now_ms += (uint32_t)ticks * 10u;
    Timer3Uart1TimeBase10ms = (uint16_t)(Timer3Uart1TimeBase10ms + ticks);
}

void sim_scan_keys(uint8_t mask, uint16_t scans)
{
    sim_key_mask = mask;
    while (scans--) {
        sim_advance_10ms(1);
        key_scan_10ms();  /* 显式调度真实扫描函数；没有注入 NVIC 中断。 */
    }
}

void sim_sensor_frame(uint8_t low_bcd, uint8_t high_bcd, uint8_t unit)
{
    /* 来自仓库 UFL-1A 字段约定：总量 123456、温度 23.50℃；流量由 BCD 输入控制。 */
    static const uint8_t frame[30] = {
        0x3c, 0x32, 0, 0, 0, 0, 0, 0, 0x0a, 0x56, 0x34, 0x12, 0, 0, 0,
        0x1b, 0, 0x01, 0, 0, 0, 0, 0, 0, 0x0d, 0x50, 0x23, 0, 0, 0x16
    };
    uint8_t i;
    uint8_t checksum = 0;
    memcpy(Uart1RxBuffer, frame, sizeof(frame));
    Uart1RxBuffer[15] = unit;
    Uart1RxBuffer[16] = low_bcd;
    Uart1RxBuffer[17] = high_bcd;
    /* 报文构造用的加和校验，不调用生产 GetCheckSum 生成自证预期。 */
    for (i = 0; i < 28; i++) checksum = (uint8_t)(checksum + Uart1RxBuffer[i]);
    Uart1RxBuffer[28] = checksum;
    Uart1HaveData = 1;
    Uart1RxCounter = sizeof(frame);
}

void sim_coil_request(uint16_t start, uint16_t count)
{
    /* 直接调用 FC01 处理器：地址/CRC 分发不在本组覆盖范围。 */
    Uart2RxBuffer[0] = 2;
    Uart2RxBuffer[1] = 1;
    Uart2RxBuffer[2] = (uint8_t)(start >> 8);
    Uart2RxBuffer[3] = (uint8_t)start;
    Uart2RxBuffer[4] = (uint8_t)(count >> 8);
    Uart2RxBuffer[5] = (uint8_t)count;
    Uart2RxCounter = 8;
}
