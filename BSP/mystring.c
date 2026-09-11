/*
 * @Author: liyongtai
 * @Date: 2024-11-02 10:44:09
 * @LastEditTime: 2024-11-13 20:54:37
 * @LastEditors: liyongtai
 * @Description:Character string operation function set
 * @FilePath: \UMF_SOFTWARE\BSP\mystring.c
 * Copyright(c) 2020-2024 liyongtai All rights reserved
 */
/* Includes ------------------------------------------------------------------*/
#include "mystring.h"
#include "main.h"
#include "ftoa.h"
/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

/* Private user code ---------------------------------------------------------*/

/**
 * @Author: liyongtai
 * @description: The character string is shifted to the left.count:num
 * @param {char} *str
 * @param {int} num
 * @return {*}
 */

char *leftShift(char *str, int num)
{

    char *p   = str;
    int   len = strlen(str);
    if (len == 0)
        return NULL;
    else
    {
        if (len >= num)
        {
            p = p + num;
            return p;
        }
        else
            return NULL;
    }
}
char* Int2String(int num,char *str)//10进制
{
    itoa_local(num, str);
     return str;//返回转换后的值
}
void insert_char(unsigned char *str, unsigned char ch, int pos)
{
    int len = strlen((char*)str);
    for (int i = len; i >= (pos); i--)
    {
        str[i + 1] = str[i];
    }
    str[pos] = ch;
}

void u32_to_str_pad(uint32_t val, char *buf, uint8_t width)
{
    char tmp[11];
    uint8_t i = 0;

    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val > 0) {
            tmp[i++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }
    /* 补前导零 */
    while (i < width) tmp[i++] = '0';

    /* 反转到 buf */
    uint8_t j;
    for (j = 0; j < i; j++) {
        buf[j] = tmp[i - 1 - j];
    }
    buf[j] = '\0';
}


