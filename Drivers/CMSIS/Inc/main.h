/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.h
 * @brief          : Header for main.c file.
 *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C"
{
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp_usart.h"
#include "eeprom.h"

#include "oled.h"
    /* USER CODE END Includes */

    /* Exported types ------------------------------------------------------------*/
    /* USER CODE BEGIN ET */
    typedef union
    {
        uint8_t str[4];
        float   num;
    } SpanTypeDef;
    /* USER CODE END ET */

    /* Exported constants --------------------------------------------------------*/
    /* USER CODE BEGIN EC */

    /* USER CODE END EC */

    /* Exported macro ------------------------------------------------------------*/
    /* USER CODE BEGIN EM */

    /* USER CODE END EM */

    /* Exported functions prototypes ---------------------------------------------*/
    void Error_Handler(void);

    /* USER CODE BEGIN EFP */
    extern void Time_Delay(uint32_t nCount);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define K_MOV_Pin           GPIO_PIN_15
#define K_MOV_GPIO_Port     GPIOC
#define K_SUB_Pin           GPIO_PIN_0
#define K_SUB_GPIO_Port     GPIOA
#define USART2_DE_Pin       GPIO_PIN_1
#define USART2_DE_GPIO_Port GPIOA
#define OLED_SDA_Pin        GPIO_PIN_4
#define OLED_SDA_GPIO_Port  GPIOA
#define OLED_RES_Pin        GPIO_PIN_5
#define OLED_RES_GPIO_Port  GPIOA
#define OLED_DC_Pin         GPIO_PIN_6
#define OLED_DC_GPIO_Port   GPIOA
#define OLED_CS_Pin         GPIO_PIN_7
#define OLED_CS_GPIO_Port   GPIOA
#define OLED_CLK_Pin        GPIO_PIN_0
#define OLED_CLK_GPIO_Port  GPIOB
#define K_ADD_Pin           GPIO_PIN_11
#define K_ADD_GPIO_Port     GPIOA
#define PowerLed_Pin        GPIO_PIN_5
#define PowerLed_GPIO_Port  GPIOB

    /* USER CODE BEGIN Private defines */

#define DacValueStartMinAddress 20 // MODBUS:03function code DAC
#define DacValueStartMaxAddress 21
#define DacValueLength          2
    extern uint16_t DacValueBuf[2]; // da output
    extern uint16_t DacValue;
    extern uint8_t  CalEnabledFlag;
    extern uint8_t  ForceDacOutFlag;
#define DacZeroValue DacValueBuf[0]
#define DacFullValue DacValueBuf[1]

#define SpanValueStartMinAddress 30 // MODBUS:03function code span
#define SpanValueStartMaxAddress 32
#define SpanValueLength          2
    extern SpanTypeDef SpanValueBuf[2];
#define SpanLoValue (SpanValueBuf[0].num)
#define SpanHiValue (SpanValueBuf[1].num)
    extern uint16_t BitControlBuf[50]; // 位控制
#define BitBufLength           50
#define DacOutputBitCon        BitControlBuf[0]
#define DacOutput_ENABLED      DacOutputBitCon |= 0X0100 // modbus adr:0x:1
#define DacOutput_DISABLED     DacOutputBitCon &= 0XFEFF
#define DacOutputZero_ENABLED  DacOutputBitCon |= 0X0200 // modbus adr:0x:2
#define DacOutputZero_DISABLED DacOutputBitCon &= 0XFdFF
#define DacOutputFull_ENABLED  DacOutputBitCon |= 0X0400 // modbus adr:0x:3
#define DacOutputFull_DISABLED DacOutputBitCon &= 0XFbFF
#define FlowClear_ENABLED      DacOutputBitCon |= 0X0800 // modbus adr:0x:4
#define FlowClear_DISABLED     DacOutputBitCon &= 0XF7FF
#define FlowRst_ENABLED        DacOutputBitCon |= 0X0001 // modbus adr:0x:5
#define FlowRst_DISABLED       DacOutputBitCon &= 0XFFFE
#define FlowPassiveRd_ENABLED  DacOutputBitCon |= 0X0002 // modbus adr:0x:6
#define FlowPassiveRd_DISABLED DacOutputBitCon &= 0XFFFd

    extern uint8_t  KeyaddFlag;
    extern uint8_t  KeysubFlag;
    extern uint8_t  keysetFlag;
    extern uint32_t keysetTimeBase;
    extern uint32_t keyaddTimeBase;
    extern uint32_t keysubTimeBase;
    extern uint8_t  keysetTimeEnable;
    extern uint8_t  keyaddTimeEnable;
    extern uint8_t  keysubTimeEnable;
    extern uint8_t  DisplayEnabled;

    extern uint32_t keyadd10TimeBase;
    extern uint32_t keysub10TimeBase;
    extern uint8_t  keyadd10TimeEnable;
    extern uint8_t  keysub10TimeEnable;

    /* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
