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
#include "gpio.h"
#include "iwdg.h"
#include "tim.h"
#include "usart.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bmp.h"
#include "key.h"
#include "bsp_menu.h"
#include "param_storage.h"
#include "run_display.h"
#include "ssd1306.h"
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
float lin_clac_x8_y8(int32_t xn, int32_t x[], int32_t y[], int8_t m);
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
   
    MX_TIM3_Init();
    MX_TIM4_Init();
    /* USER CODE BEGIN 2 */
    DacValue = 8000;
    Data_Init();
    param_storage_init();
    bsp_usart_set_modbus_addr(param_get_modbus_addr());
    /* SpanValueBuf 同步为 param_storage 的值 (Modbus 读响应缓存) */
    SpanLoValue = param_get_value_4ma();
    SpanHiValue = param_get_value_20ma();
    key_init();
    menu_init(NULL);
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);
    __HAL_UART_CLEAR_IDLEFLAG(&huart2);
    EnableUart_IT_IDLE(&huart1, &Uart1ReceiveType);
    EnableUart_IT_IDLE(&huart2, &Uart2ReceiveType);
    FlowPassiveReadCmdEnable = 1;
    run_display_init(NULL);  /* NULL = 使用默认配置 (200ms 刷新) */
    MX_IWDG_Init();
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

        /* 运行显示刷新 (200ms) */
        if ((DisplayTimeBase >= 20) && DisplayEnabled && !menu_is_active())
        {
            DisplayTimeBase = 0;
            {
                const run_display_input_t input = {
                    .p_flow_rate    = &FlowRateValue,
                    .p_temperature  = &FlowTemperature,
                    .p_pressure     = &FlowPressure,
                    .p_cumulative   = &Cumulativeflow,
                    .p_flow_sum_buf = strFlowSumBuf,
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
        PWMConfig(&htim1, 100000, (uint8_t)(DacValue >> 8));
        PWMConfig(&htim4, 100000, (uint8_t)(DacValue >> 0));
        if ((!ForceDacOutFlag) && (!CalEnabledFlag))
        {
            /* Step 3: 仪表系数 + 介质系数 */
            float corrected_flow = FlowRateValue.num
                * param_get_meter_coeff()
                * param_get_medium_coeff();

            /* DAC 线性换算: corrected_flow → 4~20mA PWM */
            DacValue = (uint16_t)(ConvertFunc(corrected_flow,
                param_get_value_4ma(), param_get_value_20ma(),
                (float)DacZeroValue, (float)DacFullValue));

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
    RCC_OscInitStruct.OscillatorType     = RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState           = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue     = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState           = RCC_HSI_ON;
    RCC_OscInitStruct.LSIState           = RCC_LSI_ON;
    RCC_OscInitStruct.PLL.PLLState       = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource      = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL         = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
     */
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
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
    float   ret;
    int32_t lin_x[2];
    int32_t lin_y[2];
    lin_x[0] = (int32_t)x0;
    lin_x[1] = (int32_t)x1;
    lin_y[0] = (int32_t)y0;
    lin_y[1] = (int32_t)y1;
    ret      = lin_clac_x8_y8((int32_t)pv, lin_x, lin_y, 2);
    return ret;
}

/******************************************************************************
  1。函数名称：lin_clac_x8_y8()--------全程线性插值计算
  2。条件： 1。已知一条曲线的若干个标定点坐标(x[],y[])
            2. 所有标定点坐标按照递增序列排列
            3。已知该曲线上一点的X坐标序列
  3。功能： 求该点所对应的y坐标

  4。常量说明 m，数组长度
 4。入口：xn:已知的X坐标，x[]:x坐标序列;y[]:y坐标序列
 5。出口：被求点y坐标
 ******************************************************************************/
float lin_clac_x8_y8(int32_t xn, int32_t x[], int32_t y[], int8_t m)
{
    int8_t  i;
    float   yn;
    int32_t tmp;
    int32_t data_temp;
    for (i = 1; i < (m - 1); i++)
    {
        if (xn <= x[i])
            break;
    }
    tmp       = (y[i] - y[i - 1]);
    data_temp = x[i] - x[i - 1];
    if (!data_temp)
        data_temp = 1;
    yn = (float)tmp * (xn - x[i - 1]) / (float)data_temp + y[i - 1];
    return (yn);
}
/**
 * @Author: liyongtai
 * @description: data initialization
 * @return {*}
 */
void Data_Init(void)
{
    uint32_t BackupBuf[2];
    ReadBufferFlash(2, ADDR_FLASH_PAGE_63, BackupBuf);
    SpanValueBuf[0].str[0] = (uint8_t)(BackupBuf[0] >> 24);
    SpanValueBuf[0].str[1] = (uint8_t)(BackupBuf[0] >> 16);
    SpanValueBuf[0].str[2] = (uint8_t)(BackupBuf[0] >> 8);
    SpanValueBuf[0].str[3] = (uint8_t)(BackupBuf[0] >> 0);
    SpanValueBuf[1].str[0] = (uint8_t)(BackupBuf[1] >> 24);
    SpanValueBuf[1].str[1] = (uint8_t)(BackupBuf[1] >> 16);
    SpanValueBuf[1].str[2] = (uint8_t)(BackupBuf[1] >> 8);
    SpanValueBuf[1].str[3] = (uint8_t)(BackupBuf[1] >> 0);

    ReadBufferFlash_16(2, ADDR_FLASH_PAGE_64, DacValueBuf);
    if (DacZeroValue < 100)
        DacZeroValue = 12100;
    if (!DacFullValue)
        DacFullValue = 60000;
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

#ifdef USE_FULL_ASSERT
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
