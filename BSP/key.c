/*
 * @Author: liyongtai
 * @Date: 2024-11-13 13:50:53
 * @LastEditTime: 2024-11-14 18:47:19
 * @LastEditors: liyongtai
 * @Description:
 * @FilePath: \UMF_SOFTWARE\BSP\key.c
 * Copyright(c) 2020-2024 liyongtai All rights reserved
 */

/* Includes ------------------------------------------------------------------*/
#include "key.h"
#include "run_display.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/
uint8_t keySequence;

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static void KeyHandle(void);
/* Private user code ---------------------------------------------------------*/
int32_t keyDis;
uint8_t keyadd10flag;
uint8_t keysub10flag;
uint8_t keysetInit;
uint8_t keysetquitInit;
void    keyFunc(void)
{
    uint32_t BackupBuf[2];
    if (keysetFlag && (!keysetquitInit))
    {
        keysetTimeEnable = 1;
        if (keysetTimeBase >= 400)
        {
            keysetTimeEnable = 0;
            keysetTimeBase   = 0;
            keysetInit       = 0;
            DisplayEnabled   = 1;
            OLED_Clear();
            keySequence    = 0;
            CalEnabledFlag = 0;
            keysetquitInit = 1;
        }
    }
    else
    {
        keysetTimeEnable = 0;
        keysetTimeBase   = 0;
    }
    if ((!keysetFlag) && keysetquitInit)
    {
        keysetquitInit   = 0;
        keysetTimeEnable = 0;
        keysetTimeBase   = 0;
    }

    if (keysetFlag && (!keysetInit) && (!keysetquitInit)&& (!keySequence))
    {
        keysetInit = 1;
    }
    if ((!keysetFlag) && keysetInit && (!keySequence)) // push
    {
        keysetInit     = 0;
        keySequence    = 10;
        DisplayEnabled = 0;
        OLED_Clear();
        keyDis       = 555;
        KeyaddFlag   = 0;
        KeysubFlag   = 0;
        keyadd10flag = 0;
    }
    switch (keySequence)
    {

        case 0:
        {
            /* 运行模式下的页面切换 */
            if (Key_Add && (!KeyaddFlag))
            {
                KeyaddFlag = 1;
                run_display_next_page();
            }
            if (!Key_Add && KeyaddFlag)
            {
                KeyaddFlag = 0;
            }
            if (Key_Sub && (!KeysubFlag))
            {
                KeysubFlag = 1;
                run_display_prev_page();
            }
            if (!Key_Sub && KeysubFlag)
            {
                KeysubFlag = 0;
            }
        }
        break;
        case 10:
        {
            KeyHandle();
            if (keyDis <= 0)
                keyDis = 0;
            if (keyDis >= 999)
                keyDis = 999;
            OLED_ShowString(40, 0, "LOC", 16, 1);
            OLED_ShowNum(40, 24, keyDis, 3, 24, 1);
            OLED_Refresh();
            if (keysetFlag && (!keysetInit) && (keyDis == 556))
            {
                keysetInit = 1;
            }
            if ((!keysetFlag) && keysetInit) // push

            {
                keysetInit  = 0;
                keySequence = 20;
                OLED_Clear();
                keyDis = (int32_t)(SpanValueBuf[0].num * 100);
            }
        }
        break;
        case 20:
        {
            OLED_ShowString(40, 0, "Flow-L", 16, 1);
            KeyHandle();
            if (keyDis <= 0)
                keyDis = 0;
            if (keyDis >= 99999)
                keyDis = 99999;
            char buffer[10];
            Int2String(keyDis, buffer);
            if (keyDis < 10)
            {
                insert_char((uint8_t *)buffer, '0', 0);
                insert_char((uint8_t *)buffer, '0', 0);
                insert_char((uint8_t *)buffer, '.', 1);
                OLED_ShowString(72, 24, "  ", 16, 1);
            }
            else if (keyDis < 100)
            {
                insert_char((uint8_t *)buffer, '0', 0);
                insert_char((uint8_t *)buffer, '.', 1);
                OLED_ShowString(72, 24, "  ", 16, 1);
            }
            else if (keyDis < 1000)
            {
                insert_char((uint8_t *)buffer, '.', 1);
                OLED_ShowString(72, 24, "  ", 16, 1);
            }

            else if (keyDis < 10000)
            {
                insert_char((uint8_t *)buffer, '.', 2);
                OLED_ShowString(72, 24, "  ", 16, 1);
            }
            else
                insert_char((uint8_t *)buffer, '.', 3);
            OLED_ShowString(40, 24, (uint8_t *)buffer, 16, 1);
            OLED_ShowString(104, 24, "L/H", 16, 1);
            OLED_Refresh();
            if (keysetFlag && (!keysetInit))
            {
                keysetInit = 1;
            }
            if ((!keysetFlag) && keysetInit)
            {
                keysetInit  = 0;
                keySequence = 30;
                OLED_Clear();
                SpanValueBuf[0].num = (float)keyDis / 100;
                BackupBuf[0] = ((uint32_t)SpanValueBuf[0].str[0] << 24) + ((uint32_t)SpanValueBuf[0].str[1] << 16) + ((uint32_t)SpanValueBuf[0].str[2] << 8) +
                               SpanValueBuf[0].str[3];
                BackupBuf[1] = ((uint32_t)SpanValueBuf[1].str[0] << 24) + ((uint32_t)SpanValueBuf[1].str[1] << 16) + ((uint32_t)SpanValueBuf[1].str[2] << 8) +
                               SpanValueBuf[1].str[3];
                WriteBufferFlash(2, ADDR_FLASH_PAGE_63, BackupBuf);
                keyDis = (int32_t)(SpanValueBuf[1].num * 100);
            }
        }
        break;
        case 30:
        {
            OLED_ShowString(40, 0, "Flow-H", 16, 1);
            KeyHandle();
            if (keyDis <= 0)
                keyDis = 0;
            if (keyDis >= 99999)
                keyDis = 99999;
            char buffer[10];
            Int2String(keyDis, buffer);
            if (keyDis < 10)
            {
                insert_char((uint8_t *)buffer, '0', 0);
                insert_char((uint8_t *)buffer, '0', 0);
                insert_char((uint8_t *)buffer, '.', 1);
                OLED_ShowString(72, 24, "  ", 16, 1);
            }
            else if (keyDis < 100)
            {
                insert_char((uint8_t *)buffer, '0', 0);
                insert_char((uint8_t *)buffer, '.', 1);
                OLED_ShowString(72, 24, "  ", 16, 1);
            }
            else if (keyDis < 1000)
            {
                insert_char((uint8_t *)buffer, '.', 1);
                OLED_ShowString(72, 24, "  ", 16, 1);
            }

            else if (keyDis < 10000)
            {
                insert_char((uint8_t *)buffer, '.', 2);
                OLED_ShowString(72, 24, "  ", 16, 1);
            }
            else
                insert_char((uint8_t *)buffer, '.', 3);
            OLED_ShowString(40, 24, (uint8_t *)buffer, 16, 1);
            OLED_ShowString(104, 24, "L/H", 16, 1);
            OLED_Refresh();
            if (keysetFlag && (!keysetInit))
            {
                keysetInit = 1;
            }
            if ((!keysetFlag) && keysetInit)
            {
                keysetInit  = 0;
                keySequence = 40;
                OLED_Clear();
                SpanValueBuf[1].num = (float)keyDis / 100;
                BackupBuf[0] = ((uint32_t)SpanValueBuf[0].str[0] << 24) + ((uint32_t)SpanValueBuf[0].str[1] << 16) + ((uint32_t)SpanValueBuf[0].str[2] << 8) +
                               SpanValueBuf[0].str[3];
                BackupBuf[1] = ((uint32_t)SpanValueBuf[1].str[0] << 24) + ((uint32_t)SpanValueBuf[1].str[1] << 16) + ((uint32_t)SpanValueBuf[1].str[2] << 8) +
                               SpanValueBuf[1].str[3];
                WriteBufferFlash(2, ADDR_FLASH_PAGE_63, BackupBuf);
                keyDis = DacZeroValue;
            }
        }
        break;
        case 40:
        {
            CalEnabledFlag = 1;
            OLED_ShowString(40, 0, "DA-ZERO", 16, 1);
            KeyHandle();
            if (keyDis <= 0)
                keyDis = 0;
            if (keyDis >= 65535)
                keyDis = 65535;
            DacValue = keyDis;
            char buffer[10];
            Int2String(keyDis, buffer);
            if (keyDis < 10)
            {

                OLED_ShowString(48, 24, "    ", 16, 1);
            }
            else if (keyDis < 100)
            {

                OLED_ShowString(56, 24, "   ", 16, 1);
            }
            else if (keyDis < 1000)
            {

                OLED_ShowString(64, 24, "  ", 16, 1);
            }

            else if (keyDis < 10000)
            {

                OLED_ShowString(72, 24, " ", 16, 1);
            }
            else
                OLED_ShowString(80, 24, " ", 16, 1);

            OLED_ShowString(40, 24, (uint8_t *)buffer, 16, 1);
            OLED_Refresh();
            if (keysetFlag && (!keysetInit))
            {
                keysetInit = 1;
            }
            if ((!keysetFlag) && keysetInit)
            {
                keysetInit  = 0;
                keySequence = 50;
                OLED_Clear();
                DacZeroValue = keyDis;
                WriteBufferFlash_16(2, ADDR_FLASH_PAGE_64, DacValueBuf);
                keyDis = DacFullValue;
            }
        }
        break;
        case 50:
        {
            CalEnabledFlag = 1;
            OLED_ShowString(40, 0, "DA-FULL", 16, 1);
            KeyHandle();
            if (keyDis <= 0)
                keyDis = 0;
            if (keyDis >= 65535)
                keyDis = 65535;
            DacValue = keyDis;
            char buffer[10];
            Int2String(keyDis, buffer);
            if (keyDis < 10)
            {

                OLED_ShowString(48, 24, "    ", 16, 1);
            }
            else if (keyDis < 100)
            {

                OLED_ShowString(56, 24, "   ", 16, 1);
            }
            else if (keyDis < 1000)
            {

                OLED_ShowString(64, 24, "  ", 16, 1);
            }

            else if (keyDis < 10000)
            {

                OLED_ShowString(72, 24, " ", 16, 1);
            }
            else
                OLED_ShowString(80, 24, " ", 16, 1);
            OLED_ShowString(40, 24, (uint8_t *)buffer, 16, 1);
            OLED_Refresh();
            if (keysetFlag && (!keysetInit))
            {
                keysetInit = 1;
            }
            if ((!keysetFlag) && keysetInit)
            {
                keysetInit  = 0;
                keySequence = 60;
                OLED_Clear();
                DacFullValue = keyDis;
                WriteBufferFlash_16(2, ADDR_FLASH_PAGE_64, DacValueBuf);
                CalEnabledFlag = 0;
            }
        }
        break;
        case 60:
        {
            OLED_ShowString(40, 24, "END", 16, 1);
            OLED_Refresh();
            if (keysetFlag && (!keysetInit))
            {
                keysetInit = 1;
            }
            if ((!keysetFlag) && keysetInit)
            {
                keysetInit     = 0;
                DisplayEnabled = 1;
                OLED_Clear();
                keySequence = 0;
            }
        }
        break;
        case 70:
        {
        }
        break;
        case 80:
        {
        }
        break;
    }
}

void KeyHandle(void)
{

    if (Key_Add && (!KeyaddFlag))
    {
        keyaddTimeEnable = 1;
        keyaddTimeBase   = 0;
        KeyaddFlag       = 1;
        keyDis++;
        keyadd10TimeEnable = 1;
        keyadd10TimeBase   = 0;
    }
    if ((keyadd10TimeBase >= 200) && (!keyadd10flag))
    {
        keyadd10flag = 1;
        // keyaddTimeBase = 0;
    }
    if ((keyadd10TimeBase >= 300) && (keyadd10flag == 1))
    {
        keyadd10flag = 2;
        // keyaddTimeBase = 0;
    }
    if ((keyadd10TimeBase >= 400) && (keyadd10flag == 2))
    {
        keyadd10flag = 3;
        // keyaddTimeBase = 0;
    }
    if ((keyadd10TimeBase >= 500) && (keyadd10flag == 3))
    {
        keyadd10flag = 4;
        // keyaddTimeBase = 0;
    }
    switch (keyadd10flag)
    {
        case 1:
        {
            if (keyaddTimeBase >= 50)
            {
                keyDis         += 10;
                keyaddTimeBase  = 0;
            }
        }
        break;
        case 2:
        {
            if (keyaddTimeBase >= 50)
            {
                keyDis         += 100;
                keyaddTimeBase  = 0;
            }
        }
        break;
        case 3:
        {
            if (keyaddTimeBase >= 50)
            {
                keyDis         += 1000;
                keyaddTimeBase  = 0;
            }
        }
        break;
        case 4:
        {
            if (keyaddTimeBase >= 50)
            {
                keyDis         += 10000;
                keyaddTimeBase  = 0;
            }
        }
        break;
    }
    if (!Key_Add && KeyaddFlag)
    {
        KeyaddFlag         = 0;
        keyaddTimeEnable   = 0;
        keyaddTimeBase     = 0;
        keyadd10flag       = 0;
        keyadd10TimeEnable = 0;
        keyadd10TimeBase   = 0;
    }
    if (Key_Sub && (!KeysubFlag))
    {
        KeysubFlag = 1;
        keyDis--;
        keysubTimeEnable   = 1;
        keysubTimeBase     = 0;
        keysub10TimeEnable = 1;
        keysub10TimeBase   = 0;
    }
    if ((!Key_Sub) && KeysubFlag)
    {
        KeysubFlag         = 0;
        keysubTimeEnable   = 0;
        keysubTimeBase     = 0;
        keysub10flag       = 0;
        keysub10TimeEnable = 0;
        keysub10TimeBase   = 0;
    }
    if ((keysub10TimeBase >= 200) && (!keysub10flag))
    {
        keysub10flag = 1;
        // keysubTimeBase = 0;
    }
    if ((keysub10TimeBase >= 300) && (keysub10flag == 1))
    {
        keysub10flag = 2;
        // keysubTimeBase = 0;
    }
    if ((keysub10TimeBase >= 400) && (keysub10flag == 2))
    {
        keysub10flag = 3;
        // keysubTimeBase = 0;
    }
    if ((keysub10TimeBase >= 500) && (keysub10flag == 3))
    {
        keysub10flag = 4;
        // keysubTimeBase = 0;
    }
    switch (keysub10flag)
    {
        case 1:
        {
            if (keysubTimeBase >= 50)
            {
                keyDis         -= 10;
                keysubTimeBase  = 0;
            }
        }
        break;
        case 2:
        {
            if (keysubTimeBase >= 50)
            {
                keyDis         -= 100;
                keysubTimeBase  = 0;
            }
        }
        break;
        case 3:
        {
            if (keysubTimeBase >= 50)
            {
                keyDis         -= 1000;
                keysubTimeBase  = 0;
            }
        }
        break;
        case 4:
        {
            if (keysubTimeBase >= 50)
            {
                keyDis         -= 10000;
                keysubTimeBase  = 0;
            }
        }
        break;
    }
}
uint8_t keysetcount;
void    keyscan(void)
{
    if (Key_Set)
    {
        if (!keysetFlag)
            keysetcount++;
    }
    else
    {
        if (keysetFlag)
            keysetcount++;
    }
    if ((keysetcount >= 5) && (!keysetFlag))
    {
        keysetFlag  = 1;
        keysetcount = 0;
    }
    if ((keysetcount >= 5) && (keysetFlag))
    {
        keysetFlag  = 0;
        keysetcount = 0;
    }
}
