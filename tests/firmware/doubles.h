#ifndef UMF_TEST_DOUBLES_H
#define UMF_TEST_DOUBLES_H

#include "main.h"
#include "tim.h"
#include "key.h"
#include "param_storage.h"
#include "cal_table.h"
#include "ftoa.h"
#include <string.h>

enum { SIM_GPIO_WRITE = 1, SIM_DELAY, SIM_UART_TX };
#define SIM_EVENT_CAPACITY 32

typedef struct {
    uint32_t kind;
    uint32_t at_ms;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t length;
    uint32_t result;
    uint8_t data[UART_TX_LEN];
} sim_event_t;

typedef struct {
    uint16_t sample_ms;
    uint16_t window;
    float tau;
    uint8_t cal_enabled;
    float range_low;
    float range_high;
    float k[CAL_POINT_COUNT];
    float pct[CAL_POINT_COUNT];
} sim_config_t;

extern sim_config_t sim_config;
extern sim_event_t sim_events[SIM_EVENT_CAPACITY];
extern uint32_t sim_event_count;
extern uint32_t sim_boundary_error;
extern uint32_t sim_now_ms;
extern uint32_t sim_key_mask;
extern uint32_t sim_gpio_read_count;
extern HAL_StatusTypeDef sim_tx_status;
extern const char *sim_failure;
extern uint32_t ModuleRecTimes;
/* 生产文件已经导出的接收标志；测试只注入 DMA/IDLE 交付后的输入边界。 */
extern volatile uint8_t Uart1HaveData;
extern volatile uint8_t Uart1RxCounter;
extern volatile uint8_t Uart2RxCounter;
extern uint16_t getCRC16(uint8_t *ptr, uint8_t len);
void Uart1_Send_Function(void);
void Uart1_Receive_Function(void);
void Modbus_Function_1(void);

/* 表达式和行号在目标 CPU 内产生，由 Python 读取；不调用主机 assert。 */
#define CHECK(expr) do { if (!(expr)) { sim_failure = #expr; return __LINE__; } } while (0)
#define NEAR(actual, expected) (((actual) > (expected) - 0.0001f) && \
                                ((actual) < (expected) + 0.0001f))

void sim_advance_10ms(uint16_t ticks);
void sim_scan_keys(uint8_t mask, uint16_t scans);
void sim_sensor_frame(uint8_t low_bcd, uint8_t high_bcd, uint8_t unit);
void sim_coil_request(uint16_t start, uint16_t count);

#endif
