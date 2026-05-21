/**
 * @file    ftoa.h
 * @brief   轻量级 float→string 转换 (纯整数运算, 不依赖 printf %f)
 *
 * 替代 snprintf(buf, len, "%.Xf", val) 以消除 printf 浮点库依赖, 节省 3~8KB Flash。
 */
#ifndef __FTOA_H
#define __FTOA_H

#include <stdint.h>

/**
 * @brief  将 float 转为指定小数位的十进制字符串
 * @param  val      待转换值
 * @param  decimals 小数位数 (0~3)
 * @param  buf      输出缓冲区
 * @param  buf_len  缓冲区长度
 * @note   支持 [-9999.9, 99999.9] 范围, 超出范围输出 "----"
 */
void ftoa(float val, uint8_t decimals, char *buf, uint8_t buf_len);

/**
 * @brief  整数转十进制字符串 (替代 sprintf(str, "%d", num))
 * @param  num  待转换整数
 * @param  buf  输出缓冲区
 * @return buf 指针
 */
char *itoa_local(int num, char *buf);

#endif /* __FTOA_H */
