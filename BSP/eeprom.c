/**
 ******************************************************************************
 * @file    EEPROM_Emulation/src/eeprom.c
 * @author  MCD Application Team
 * @brief   This file provides all the EEPROM emulation firmware functions.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2016 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

/** @addtogroup EEPROM_Emulation
 * @{
 */

/* Includes ------------------------------------------------------------------*/
#include "eeprom.h"

/*******************************************************************************
 * Function Name  : WriteBufferFlash(uint8_t Len,uint32_t Page_Address,uint32_t WriteBuffer[])
 * Description    : 写入Len个数据,第一个固定为零，后Len-1个为需要保存的AD校验数据
 * Input          : None
 * Output         : None
 * Return         : None
 *******************************************************************************/
uint16_t WriteBufferFlash(uint8_t Len, uint32_t Page_Address, uint32_t WriteBuffer[])
{
    uint8_t  temp;
    uint32_t TopData;
    uint32_t Address;
    uint8_t  Length, fillcount;
    Length                             = Len + 1; // 每次存储一个头部校验码0x00000000
    fillcount                          = (240 / Length);
    HAL_StatusTypeDef      flashstatus = HAL_OK;
    uint32_t               page_error  = 0;
    FLASH_EraseInitTypeDef s_eraseinit;

    s_eraseinit.TypeErase   = FLASH_TYPEERASE_PAGES;
    s_eraseinit.PageAddress = Page_Address;
    s_eraseinit.NbPages     = 1;
    /* Unlock the Flash Program Erase controller */
    HAL_FLASH_Unlock();
    // FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);
    Address = Page_Address - Length * 4;
    TopData = 0;
    temp    = 0;
    while ((temp < fillcount) && (TopData != 0xffffffff)) // 读出数据
    {
        Address = Address + Length * 4;
        TopData = (*(uint32_t *)Address);
        temp++;
    }
    if (TopData != 0xffffffff)
    {

        flashstatus = HAL_FLASHEx_Erase(&s_eraseinit, &page_error);
        /* If erase operation was failed, a Flash error code is returned */
        if (flashstatus != HAL_OK)
        {
            return flashstatus;
        }
        //
        Address = Page_Address;
    } // 擦除1K缓冲区
    //--------------------------------------------------------------------------------------------------------------
    temp        = 0;
    flashstatus = HAL_OK; // 写入6个数据
    while ((temp < Length) && (flashstatus == HAL_OK))
    {
        if (!temp)
            //  FLASHStatus = FLASH_ProgramWord(Address, 0);
            flashstatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, Address, 0);
        else
            // FLASHStatus = FLASH_ProgramWord(Address, WriteBuffer[temp - 1]);
            flashstatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, Address, WriteBuffer[temp - 1]);
        Address = Address + 4;
        temp++;
    }
    HAL_FLASH_Lock();
    return flashstatus;
}

/*******************************************************************************
 * Function Name  : WriteBufferFlash(uint8_t Len,uint32_t Page_Address,s32 WriteBuffer[])
 * Description    : 写入Len个数据,第一个固定为零，后Len-1个为需要保存的AD校验数据
 * Input          : None
 * Output         : None
 * Return         : None
 *******************************************************************************/
uint16_t WriteBufferFlash_16(uint8_t Len, uint32_t Page_Address, uint16_t WriteBuffer[])
{
    uint8_t  temp;
    uint16_t TopData;
    uint32_t Address;
    uint8_t  Length, fillcount;
    Length                             = Len + 1; // 每次存储一个头部校验码0x0000
    fillcount                          = (480 / Length);
    HAL_StatusTypeDef      flashstatus = HAL_OK;
    uint32_t               page_error  = 0;
    FLASH_EraseInitTypeDef s_eraseinit;

    s_eraseinit.TypeErase   = FLASH_TYPEERASE_PAGES;
    s_eraseinit.PageAddress = Page_Address;
    s_eraseinit.NbPages     = 1;
    /* Unlock the Flash Program Erase controller */
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPERR);
    Address = Page_Address - Length * 2;
    TopData = 0;
    temp    = 0;
    while ((temp < fillcount) && (TopData != 0xffff)) // 读出数据
    {
        Address = Address + Length * 2;
        TopData = (*(uint16_t *)Address);
        temp++;
    }
    if (TopData != 0xffff)
    {

        flashstatus = HAL_FLASHEx_Erase(&s_eraseinit, &page_error);
        /* If erase operation was failed, a Flash error code is returned */
        if (flashstatus != HAL_OK)
        {
            return flashstatus;
        }

        Address = Page_Address;
    } // 擦除1K缓冲区
    //--------------------------------------------------------------------------------------------------------------
    temp        = 0;
    flashstatus = HAL_OK; // 写入6个数据
    while ((temp < Length) && (flashstatus == HAL_OK))
    {
        if (!temp)
            //  FLASHStatus = FLASH_ProgramWord(Address, 0);
            flashstatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, Address, 0);
        else
            // FLASHStatus = FLASH_ProgramWord(Address, WriteBuffer[temp - 1]);
            flashstatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, Address, WriteBuffer[temp - 1]);
        Address = Address + 2;
        temp++;
    }
    HAL_FLASH_Lock();
    return flashstatus;
}

/*******************************************************************************
 * Function Name  : ReadBufferFlash(uint8_t Len,uint32_t Page_Address,s32 ReadBuffer[])
 * Description    : 读Len个数据
 * Input          : None
 * Output         : None
 * Return         : None
 *******************************************************************************/
void ReadBufferFlash(uint8_t Len, uint32_t Page_Address, uint32_t ReadBuffer[])
{
    uint8_t  temp;
    uint8_t  i;
    uint32_t TopData;
    uint32_t Address;
    uint8_t  Length;
    uint8_t  fillcount;
    /* 预置哨兵值: Flash 为空时调用方通过判断 0xFFFFFFFFu 可知未找到有效数据 */
    for (i = 0; i < Len; i++) ReadBuffer[i] = 0xFFFFFFFFu;
    Length    = Len + 1;
    fillcount = 240 / Length;
    Address = Page_Address - Length * 4;
    TopData = 0;
    temp    = 0;
    while (temp < fillcount) // 读出数据
    {
        Address = Address + Length * 4;
        TopData = (*(uint32_t *)Address);
        if ((TopData == 0) && ((*(uint32_t *)(Address + Length * 4)) == 0xffffffff))
        {
            temp    = fillcount;
            Address = Address + 4; // 取AD转换的数据，第一个字是链表首标志
            for (i = 0; i < Len; i++)
            {
                ReadBuffer[i] = *(uint32_t *)Address;
                Address       = Address + 4;
            }
        }
        temp++;
    }
}

/*******************************************************************************
 * Function Name  : ReadBufferFlash(uint8_t Len,uint32_t Page_Address,s32 ReadBuffer[])
 * Description    : 读Len个数据
 * Input          : None
 * Output         : None
 * Return         : None
 *******************************************************************************/
void ReadBufferFlash_16(uint8_t Len, uint32_t Page_Address, uint16_t ReadBuffer[])
{
    uint8_t  temp;
    uint8_t  i;
    uint16_t TopData;
    uint32_t Address;
    uint8_t  Length;
    uint8_t  fillcount;
    /* 预置哨兵值: Flash 为空时调用方通过判断 0xFFFFu 可知未找到有效数据 */
    for (i = 0; i < Len; i++) ReadBuffer[i] = 0xFFFFu;
    Length    = Len + 1;
    fillcount = 480 / Length;
    Address = Page_Address - Length * 2;
    TopData = 0;
    temp    = 0;
    while (temp < fillcount) // 读出数据
    {
        Address = Address + Length * 2;
        TopData = (*(uint16_t *)Address);
        if ((TopData == 0) && ((*(uint16_t *)(Address + Length * 2)) == 0xffff))
        {
            temp    = fillcount;
            Address = Address + 2; // 取AD转换的数据，第一个字是链表首标志
            for (i = 0; i < Len; i++)
            {
                ReadBuffer[i] = *(uint16_t *)Address;
                Address       = Address + 2;
            }
        }
        temp++;
    }
}
/**
 * @}
 */
