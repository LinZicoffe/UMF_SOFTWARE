/*
 * @Author      : liyongtai
 * @Date        : 2024-11-02 10: 44: 30
 * @LastEditTime: 2024-11-13 21:10:06
 * @LastEditors: liyongtai
 * @Description : Character string operation function heander files
 * @FilePath: \UMF_SOFTWARE\BSP\mystring.h
 * Copyright(c) 2020-2024 liyongtai All rights reserved
 */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef _MYSTRING_H_
#define _MYSTRING_H_
/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <string.h>
/* Exported constants --------------------------------------------------------*/
/* Exported types ------------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
char *leftShift(char *str, int num);
char* Int2String(int num,char *str);//10进制
void insert_char(unsigned char *str, unsigned char ch, int pos);
void u32_to_str_pad(uint32_t val, char *buf, uint8_t width);
#endif /* _MYSTRING_H_ */
