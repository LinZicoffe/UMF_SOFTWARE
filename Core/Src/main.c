/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "iwdg.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bmp.h"
#include "key.h"
#include "bsp_menu.h"
#include "param_storage.h"
#include "bsp_usart.h"
#include "boot_flag.h"
#include "run_display.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "cal_table.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

uint16_t    BitControlBuf[50]; // 位控制
uint16_t    DacValueBuf[2];    // da output
SpanTypeDef SpanValueBuf[2];
uint16_t    DacValue;
uint8_t     CalEnabledFlag;
uint8_t     ForceDacOutFlag;
uint8_t     DisplayEnabled = 1;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void  Time_Delay(uint32_t nCount);
void  FlashProtect(void);
void  Data_Init(void);
float ConvertFunc(float pv, float x0, float x1, float y0, float y1);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**

* @Author: liyongtai
* @description:
* @param {uint32_t} nCount
* @return {*}
*/
void Time_Delay(uint32_t nCount)
{
    for (; nCount != 0; nCount--)
        ;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
    /* §5.4 启动序列第一步（须在任何外设初始化前）：
     * CSTACK 上界断言 + 读取启动期邮箱快照（40130 回显源）。*/
    boot_flag_init();

    /* BL 跳转后 IWDG 仍在运行（无法停止，仅复位可关）：先喂狗消除 BL 残余
     * 计数的立即复位风险，随即放宽到 ~1s（前置清单 5：Prescaler 64 + 624
     * → 40kHz/64=625Hz，625 计数=1.0s），覆盖参数迁移（含页擦 20~40ms×n）
     * 与 OLED 等长初始化路径。*/
    IWDG->KR = 0xAAAAu;             /* 喂狗 */
    IWDG->KR = 0x5555u;             /* 允许写 PR/RLR */
    IWDG->PR = IWDG_PRESCALER_64;
    IWDG->RLR = 624u;
    IWDG->KR = 0xAAAAu;             /* 以新值立即重载 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_IWDG_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  /* USER CODE BEGIN 2 */
    /* 顺序硬性要求（方案 §5.4 ①）：参数存储初始化（含旧数据迁移）必须早于
     * Data_Init()——后者现在从统一参数存储同步 DAC/Span（store → RAM 镜像）。*/
    param_storage_init();
    Data_Init();
    DacValue = DacZeroValue;/* 初始化为零点值 (4mA)，避免初始输出异常 */
    //DacValue = 800;
    bsp_usart_set_modbus_addr(param_get_modbus_addr());
    bsp_usart2_apply_uart_config(param_get_uart_config());
    key_init();
    menu_init(NULL);
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);
    __HAL_UART_CLEAR_IDLEFLAG(&huart2);
    EnableUart_IT_IDLE(&huart1, &Uart1ReceiveType);
    EnableUart_IT_IDLE(&huart2, &Uart2ReceiveType);
    FlowPassiveReadCmdEnable = 1;
    run_display_init(NULL);  /* NULL = 使用默认配置 (200ms 刷新) */
    MX_IWDG_Init();
    /* §5.4 启动序列收尾：参数迁移已确认完成（param_storage_init 返回），
     * 清除邮箱 cmd 并清零 G3 计数。此后发生的复位（看门狗/断电）由 BL
     * 按 App 有效性与 G3 重新判定，不会误入升级模式。*/
    boot_mailbox_clear();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

        /* 按键事件 + 菜单处理 */
        {
            key_event_t evt = key_get_event();
            menu_status_t menu_st;
            if (menu_process(evt, &menu_st)) {
                /* 菜单已激活并渲染 */
            }
            else if (evt == KEY_UP && !menu_is_active()) {
                run_display_prev_page();
            }
            else if (evt == KEY_DOWN && !menu_is_active()) {
                run_display_next_page();
            }
        }

        /* OLED 抗干扰自愈: 周期性重发 SSD1306 配置命令
         * 间隔由 param_get_oled_recovery_interval() 控制 (单位 100ms, 即 10×10ms)
         * 0 = 禁用; 默认 50 = 5 秒
         * 仅在菜单未激活时执行, 避免重初始化打断菜单交互 */
        {
            uint16_t recovery_interval = param_get_oled_recovery_interval();
            if (recovery_interval > 0 &&
                OledRecoveryTimeBase >= (uint16_t)(recovery_interval * 10) &&
                !menu_is_active())
            {
                OledRecoveryTimeBase = 0;
                ssd1306_RecoveryInit();
            }
        }

        /* 运行显示刷新 (200ms) */
        if ((DisplayTimeBase >= 20) && DisplayEnabled && !menu_is_active())
        {
            DisplayTimeBase = 0;
            {
                Uart_SendfloatTypeDef eff_rate;
                Uart_SendfloatTypeDef eff_temp;
                eff_rate.num = convert_flow_rate_from_lph(
                    effective_flow_rate(),
                    param_get_flow_unit(),
                    param_get_medium_density());
                eff_temp.num = effective_temperature();

                const run_display_input_t input = {
                    .p_flow_rate    = &eff_rate,
                    .p_temperature  = &eff_temp,
                    .p_pressure     = &FlowPressure,
                    .p_cumulative   = &Cumulativeflow,
                    .p_flow_sum_buf = effective_flow_sum_buf(strFlowSumBuf),
                    .p_sum_unit     = &Sumunit,
                    .p_module_state = &ModuleState,
                    .p_dac_value    = &DacValue,
                    .p_dac_buf      = DacValueBuf,
                    .p_flow_unit_str  = param_get_flow_unit_str(param_get_flow_unit()),
                    .p_total_unit_str = param_get_total_unit_str(param_get_total_unit()),
                };
                run_display_render(&input);
            }
            ssd1306_UpdateScreen();
        }

        /* 原有逻辑 */
        HAL_IWDG_Refresh(&hiwdg);
        Uart1_Communication();
        Uart2_Communication();

        /* DAC 换算 (先计算，再输出 PWM) */
        if ((!ForceDacOutFlag) && (!CalEnabledFlag))
        {
            /* Step 3: 仪表系数 + 介质系数 */
            float corrected_flow = effective_flow_rate()
                * param_get_meter_coeff()
                * param_get_medium_coeff();

            /* 七点标定修正 */
            corrected_flow = cal_correct_flow(corrected_flow);

            /* DAC 线性换算: corrected_flow → 4~20mA PWM */
            {
                float dac_raw = ConvertFunc(corrected_flow,
                    param_get_value_4ma(), param_get_value_20ma(),
                    (float)DacZeroValue, (float)DacFullValue);
                /* Clamp: 防止负值或超限值导致 uint16_t 异常 */
                if (dac_raw < (float)DacZeroValue) dac_raw = (float)DacZeroValue;
                if (dac_raw > (float)DacFullValue)  dac_raw = (float)DacFullValue;
                DacValue = (uint16_t)dac_raw;
            }

            /* Step 4: 小信号切除 — 流量低于 [量程下限 + 量程×N%] 时输出零点 */
            {
                float span = param_get_value_20ma() - param_get_value_4ma();
                if (span > 0.0f) {
                    float threshold = span * param_get_small_signal() / 100.0f;
                    if (corrected_flow < (param_get_value_4ma() + threshold))
                        DacValue = DacZeroValue;
                }
            }
        }

        /* PWM 输出 — 在 DAC 换算之后，确保使用最新 DacValue */
        PWMConfig(&htim1, 100000, (uint8_t)(DacValue >> 8));
        PWMConfig(&htim4, 100000, (uint8_t)(DacValue >> 0));
    }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/**
 * @Author: liyongtai
 * @description: flash program protect
 * @return {*}
 */
void FlashProtect(void)
{
    FLASH_OBProgramInitTypeDef obInit;
    HAL_FLASHEx_OBGetConfig(&obInit);
    if (obInit.RDPLevel != OB_RDP_LEVEL_1)
    {
        HAL_FLASH_Unlock();
        HAL_FLASH_OB_Unlock();
        obInit.OptionType = OPTIONBYTE_RDP;
        obInit.RDPLevel   = OB_RDP_LEVEL_1;
        HAL_FLASHEx_OBProgram(&obInit);
        HAL_FLASH_OB_Lock();
        HAL_FLASH_Lock();
    }
}
/**
 * @Author: liyongtai
 * @description: 线性处理函数
 * @param {float} pv
 * @param {float} x0
 * @param {float} x1
 * @param {float} y0
 * @param {float} y1
 * @return {*}
 */
float ConvertFunc(float pv, float x0, float x1, float y0, float y1)
{
    float span = x1 - x0;
    if (span == 0.0f) return y0;
    return y0 + (pv - x0) * (y1 - y0) / span;
}
/**
* @Author: liyongtai
* @description: data initialization
* @return {*}
*/
void Data_Init(void)
{
    /* A4 重写：DAC/Span 一律经统一参数存储（param_storage_init 已完成
     * 选页/迁移/载入），方向为 store → RAM 镜像；旧 Page 63/59 直读废弃。*/
    DacValueBuf[0] = param_get_dac_zero();
    DacValueBuf[1] = param_get_dac_full();
    SpanLoValue    = param_get_value_4ma();
    SpanHiValue    = param_get_value_20ma();

    /* Span 默认值保护: 异常值（含迁移缺失回落默认失败）时恢复默认 */
    if (SpanLoValue >= SpanHiValue) { SpanLoValue = 0.0f; SpanHiValue = 100.0f; }

    if (DacZeroValue < 100)
        DacZeroValue = 12100;
    if (!DacFullValue)
        DacFullValue = 60000;
    if (DacZeroValue >= DacFullValue) { DacZeroValue = 12100; DacFullValue = 60000; }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
