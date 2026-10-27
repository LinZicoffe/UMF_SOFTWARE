#include "doubles.h"

uint32_t test_smoke_float_format(void)
{
    char buf[16];
    ftoa(1.234f, 3, buf, sizeof(buf));
    CHECK(strcmp(buf, "1.234") == 0);
    ftoa(-12.5f, 1, buf, sizeof(buf));
    CHECK(strcmp(buf, "-12.5") == 0);
    ftoa(100000.0f, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "----") == 0);
    return 0;
}

uint32_t test_keys_threshold_and_release(void)
{
    key_init();
    sim_scan_keys(1, 1);  /* 10ms，未消抖 */
    sim_scan_keys(0, 2);
    CHECK(key_get_event() == KEY_NONE);
    sim_scan_keys(1, 2);  /* 20ms，达到阈值；按住不产生事件 */
    CHECK(key_get_event() == KEY_NONE);
    sim_scan_keys(0, 1);
    CHECK(key_get_event() == KEY_NONE);
    sim_scan_keys(0, 1);
    CHECK(key_get_event() == KEY_DOWN);
    CHECK(key_get_event() == KEY_NONE);
    CHECK(sim_gpio_read_count == 21);
    return 0;
}

uint32_t test_keys_single_mapping(void)
{
    key_init();
    sim_scan_keys(2, 2); sim_scan_keys(0, 2);
    CHECK(key_get_event() == KEY_UP);
    sim_scan_keys(4, 2); sim_scan_keys(0, 2);
    CHECK(key_get_event() == KEY_ENTER);
    return 0;
}

uint32_t test_keys_chords_and_priority(void)
{
    key_init();
    sim_scan_keys(1, 2); sim_scan_keys(5, 2); sim_scan_keys(0, 2);
    CHECK(key_get_event() == KEY_BACK);
    sim_scan_keys(7, 2); sim_scan_keys(0, 2);
    CHECK(key_get_event() == KEY_HOME);
    return 0;
}

uint32_t test_keys_bounce_hold_and_restart(void)
{
    key_init();
    sim_scan_keys(1, 1); sim_scan_keys(0, 1); sim_scan_keys(1, 300);
    CHECK(key_get_event() == KEY_NONE);  /* 消抖计数饱和后仍只在释放触发 */
    sim_scan_keys(0, 2);
    CHECK(key_get_event() == KEY_DOWN);
    sim_scan_keys(4, 2);
    key_init();  /* 停止后重新初始化，不应带出旧的按下状态 */
    sim_scan_keys(0, 2);
    CHECK(key_get_event() == KEY_NONE);
    sim_scan_keys(2, 2); sim_scan_keys(0, 2);
    CHECK(key_get_event() == KEY_UP);
    return 0;
}

uint32_t test_calibration_disabled_and_bad_span(void)
{
    CHECK(NEAR(cal_correct_flow(12.5f), 12.5f));
    sim_config.cal_enabled = 1;
    sim_config.range_high = 0;
    CHECK(NEAR(cal_correct_flow(12.5f), 12.5f));
    sim_config.range_high = -1;
    CHECK(NEAR(cal_correct_flow(12.5f), 12.5f));
    return 0;
}

uint32_t test_calibration_nodes_and_interpolation(void)
{
    static const float flow[] = {0, 10, 20, 40, 60, 80, 100};
    static const float expected[] = {0, 12.5f, 30, 70, 120, 180, 250};
    uint8_t i;
    sim_config.cal_enabled = 1;
    for (i = 0; i < 7; i++) CHECK(NEAR(cal_correct_flow(flow[i]), expected[i]));
    /* 10~20 区间中点：系数 1.375，独立手算结果 15×1.375=20.625。 */
    CHECK(NEAR(cal_correct_flow(15), 20.625f));
    CHECK(NEAR(cal_correct_flow(9), 11.025f));
    CHECK(NEAR(cal_correct_flow(11), 14.025f));
    return 0;
}

uint32_t test_calibration_clamps_and_offset(void)
{
    sim_config.cal_enabled = 1;
    CHECK(NEAR(cal_correct_flow(-10), -10));
    CHECK(NEAR(cal_correct_flow(110), 275));
    sim_config.range_low = 10;
    sim_config.range_high = 110;
    CHECK(NEAR(cal_correct_flow(25), 34.375f));
    return 0;
}

uint32_t test_sensor_bcd_fields_and_units(void)
{
    ModuleRecTimes = 5;
    sim_sensor_frame(0x00, 0x01, 0x1b);  /* 100 L/h */
    Uart1_Receive_Function();
    CHECK(NEAR(FlowRateValue.num, 100));
    CHECK(NEAR(FlowTemperature.num, 23.5f));
    CHECK(Cumulativeflow == 123456u);
    CHECK(strcmp((char *)strFlowSumBuf, "000000123.456") == 0);
    CHECK(ModuleRecTimes == 0 && Uart1HaveData == 0 && Uart1RxCounter == 0);
    sim_sensor_frame(0x00, 0x01, 0x0b);  /* 另一倍率：100/100=1 L/h */
    Uart1_Receive_Function();
    CHECK(NEAR(FlowRateValue.num, 1));
    return 0;
}

uint32_t test_sensor_invalid_frames_keep_previous(void)
{
    uint8_t length;
    FlowRateValue.num = 7;
    ModuleRecTimes = 4;
    for (length = 0; length < 28; length++) {
        sim_sensor_frame(0, 1, 0x1b);
        Uart1RxCounter = length;
        Uart1_Receive_Function();
        CHECK(NEAR(FlowRateValue.num, 7) && ModuleRecTimes == 4);
        CHECK(Uart1HaveData == 0 && Uart1RxCounter == 0);
    }
    sim_sensor_frame(0, 1, 0x1b); Uart1RxBuffer[28] ^= 1;
    Uart1_Receive_Function();
    CHECK(NEAR(FlowRateValue.num, 7) && ModuleRecTimes == 4);
    sim_sensor_frame(0, 1, 0x1b); Uart1RxBuffer[29] = 0;
    Uart1_Receive_Function();
    CHECK(NEAR(FlowRateValue.num, 7) && ModuleRecTimes == 4);
    sim_sensor_frame(0, 1, 0x1b); Uart1RxBuffer[0]++; Uart1RxBuffer[28]++;
    Uart1_Receive_Function();
    CHECK(NEAR(FlowRateValue.num, 7) && ModuleRecTimes == 4);
    CHECK(sim_event_count == 0);
    return 0;
}

uint32_t test_sensor_window_outlier_and_reset(void)
{
    sim_sensor_frame(0, 1, 0x1b); Uart1_Receive_Function();
    CHECK(NEAR(effective_flow_rate(), 100));
    sim_sensor_frame(0, 9, 0x1b); Uart1_Receive_Function();
    CHECK(NEAR(effective_flow_rate(), 100)); /* 窗口 [100,900] 去最大值，仍是 100 */
    sim_sensor_frame(0, 3, 0x1b); Uart1_Receive_Function();
    CHECK(NEAR(effective_flow_rate(), 200)); /* [900,300]→300；α=0.5 后为 200 */
    sim_config.window = 3;
    CHECK(NEAR(effective_flow_rate(), 300)); /* 改配置后回落原始值，旧滤波状态清空 */
    sim_sensor_frame(0, 2, 0x1b); Uart1_Receive_Function();
    CHECK(NEAR(effective_flow_rate(), 200));
    return 0;
}

uint32_t test_sensor_default_window_and_lowpass(void)
{
    sim_config.window = 0; sim_config.tau = 0; sim_config.sample_ms = 0;
    sim_sensor_frame(0, 1, 0x1b); Uart1_Receive_Function();
    sim_sensor_frame(0, 4, 0x1b); Uart1_Receive_Function();
    CHECK(NEAR(effective_flow_rate(), 200)); /* 默认 τ=1s,dt=0.5s，100→400 后为200 */
    return 0;
}

uint32_t test_sensor_poll_before_at_after_threshold(void)
{
    static const uint8_t request[] = {0xfe, 0xfe, 0x11, 0x5b, 0xfd, 0x58, 0x16};
    FlowPassiveReadCmdFlag = 1;
    Uart1_Send_Function();  /* 先同步配置，不能偷偷跳过首次计时器复位 */
    sim_advance_10ms(49); Uart1_Send_Function();
    CHECK(sim_event_count == 0);
    sim_advance_10ms(1); Uart1_Send_Function();
    CHECK(sim_event_count == 1 && sim_events[0].length == sizeof(request));
    CHECK(memcmp(sim_events[0].data, request, sizeof(request)) == 0);
    CHECK(sim_events[0].at_ms == 500);
    sim_advance_10ms(51); Uart1_Send_Function();
    CHECK(sim_event_count == 2 && Timer3Uart1TimeBase10ms == 0);
    return 0;
}

uint32_t test_sensor_timeout_and_recovery(void)
{
    uint8_t i;
    Uart1_Send_Function();
    for (i = 0; i < 5; i++) { sim_advance_10ms(50); Uart1_Send_Function(); }
    CHECK(ModuleState == 0 && ModuleRecTimes == 5);
    sim_advance_10ms(49); Uart1_Send_Function();
    CHECK(ModuleState == 0);
    sim_advance_10ms(1); Uart1_Send_Function();
    CHECK(ModuleState == 1 && ModuleRecTimes == 6);
    sim_sensor_frame(0, 1, 0x1b); Uart1_Receive_Function();
    CHECK(ModuleRecTimes == 0);
    /* 当前实现只在下一采样点更新 ModuleState；并未假设收到帧后立即清零。 */
    sim_advance_10ms(50); Uart1_Send_Function();
    CHECK(ModuleState == 0 && ModuleRecTimes == 1);
    return 0;
}

uint32_t test_sensor_interval_change_and_task_interleave(void)
{
    FlowPassiveReadCmdFlag = 1;
    Uart1_Send_Function();
    sim_advance_10ms(49);
    sim_config.sample_ms = 101; /* 10ms 基准向上取整到 110ms */
    Uart1_Send_Function();
    CHECK(Timer3Uart1TimeBase10ms == 0 && sim_event_count == 0);
    key_init(); sim_scan_keys(1, 2);
    sim_advance_10ms(8); Uart1_Send_Function();
    CHECK(sim_event_count == 0);
    sim_advance_10ms(1); Uart1_Send_Function();
    CHECK(sim_event_count == 1);
    sim_scan_keys(0, 2); CHECK(key_get_event() == KEY_DOWN);
    return 0;
}

uint32_t test_sensor_unit_conversion(void)
{
    CHECK(NEAR(convert_flow_rate_from_lph(600, FLOW_UNIT_M3H, 1000), 0.6f));
    CHECK(NEAR(convert_flow_rate_from_lph(600, FLOW_UNIT_LH, 1000), 600));
    CHECK(NEAR(convert_flow_rate_from_lph(600, FLOW_UNIT_LMIN, 1000), 10));
    CHECK(NEAR(convert_flow_rate_from_lph(600, FLOW_UNIT_KGH, 800), 480));
    return 0;
}

uint32_t test_protocol_crc_reference_vector(void)
{
    uint8_t vector[] = "123456789";
    /* CRC-16/MODBUS 公认 check 值，低字节在线上传输在前。 */
    CHECK(getCRC16(vector, 9) == 0x4b37);
    return 0;
}

uint32_t test_protocol_coils_complete_response_and_release(void)
{
    static const uint8_t response[] = {2, 1, 2, 0x81, 0x01, 0x5c, 0x6c};
    BitControlBuf[0] = 0x8101;  /* 协议字节顺序是低线圈位在前，跨16位 */
    sim_coil_request(0, 16);
    Modbus_Function_1();
    CHECK(sim_event_count == 3);
    CHECK(sim_events[0].kind == SIM_GPIO_WRITE && sim_events[0].result == GPIO_PIN_SET);
    CHECK(sim_events[0].arg0 == (uint32_t)GPIOA && sim_events[0].arg1 == USART2_DE_Pin);
    CHECK(sim_events[1].kind == SIM_DELAY && sim_events[1].arg0 == 20);
    CHECK(sim_events[2].kind == SIM_UART_TX && sim_events[2].arg0 == (uint32_t)USART2);
    CHECK(sim_events[2].length == sizeof(response));
    CHECK(memcmp(sim_events[2].data, response, sizeof(response)) == 0);
    /* 由测试显式提供发送完成回调；DMA IFCR 写仅落到模拟内存。 */
    HAL_UART_TxCpltCallback(&huart2);
    CHECK(sim_event_count == 5);
    CHECK(sim_events[3].kind == SIM_DELAY && sim_events[3].arg0 == 10);
    CHECK(sim_events[4].kind == SIM_GPIO_WRITE && sim_events[4].result == GPIO_PIN_RESET);
    CHECK(huart2.gState == HAL_UART_STATE_READY);
    return 0;
}

uint32_t test_protocol_coils_zero_and_overlong_are_silent(void)
{
    sim_coil_request(0, 0); Modbus_Function_1();
    CHECK(sim_event_count == 0);
    sim_coil_request(0, 1161); Modbus_Function_1();
    CHECK(sim_event_count == 0 && Uart2RxCounter == 0);
    return 0;
}

/* 已知缺陷组是失败测试，不能把不符合协议的现状当成期望正确。 */
uint32_t test_known_coils_padding_must_be_zero(void)
{
    BitControlBuf[0] = 0xff00;
    sim_coil_request(0, 1); Modbus_Function_1();
    CHECK(sim_event_count == 3);
    CHECK(sim_events[2].data[3] == 0x01); /* Modbus §6.1：未使用高7位必须为0 */
    return 0;
}

uint32_t test_protocol_busy_and_explicit_completion(void)
{
    /* HAL_BUSY 表示已有传输。本例显式注入那次传输的完成回调。 */
    sim_tx_status = HAL_BUSY;
    huart2.gState = HAL_UART_STATE_BUSY_TX;
    sim_coil_request(0, 16); Modbus_Function_1();
    CHECK(sim_event_count == 3 && sim_events[2].result == HAL_BUSY);
    HAL_UART_TxCpltCallback(&huart2);
    CHECK(sim_event_count == 5 && sim_events[4].kind == SIM_GPIO_WRITE);
    CHECK(sim_events[4].result == GPIO_PIN_RESET);
    return 0;
}
