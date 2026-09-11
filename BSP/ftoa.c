/**
 * @file    ftoa.c
 * @brief   轻量级 float→string 转换实现 (纯整数运算)
 */
#include "ftoa.h"
#include <string.h>

/* 整数转字符串, 返回 buf 指针 */
char *itoa_local(int num, char *buf)
{
    int i = 0;
    int is_neg = 0;
    unsigned int unum;

    if (num < 0) {
        is_neg = 1;
        unum = (unsigned int)(-(num + 1)) + 1u;
    } else {
        unum = (unsigned int)num;
    }

    if (unum == 0) {
        buf[i++] = '0';
    } else {
        while (unum > 0) {
            buf[i++] = (char)('0' + (int)(unum % 10u));
            unum /= 10u;
        }
    }

    if (is_neg) buf[i++] = '-';
    buf[i] = '\0';

    /* 反转 */
    {
        int lo = 0, hi = i - 1;
        while (lo < hi) {
            char tmp = buf[lo];
            buf[lo] = buf[hi];
            buf[hi] = tmp;
            lo++; hi--;
        }
    }
    return buf;
}

/* 10 的幂查找表 (decimals 0~3) */
static const uint32_t s_pow10[] = { 1u, 10u, 100u, 1000u };

void ftoa(float val, uint8_t decimals, char *buf, uint8_t buf_len)
{
    int is_neg = 0;
    uint32_t int_part, frac_part;
    uint32_t scale;
    uint8_t pos = 0;
    char tmp[12];
    uint8_t tmp_len;

    if (decimals > 3) decimals = 3;
    scale = s_pow10[decimals];

    /* 处理负数 */
    if (val < 0.0f) {
        is_neg = 1;
        val = -val;
    }

    /* 范围保护 */
    if (val > 99999.0f) {
        if (buf_len >= 5) { buf[0]='-'; buf[1]='-'; buf[2]='-'; buf[3]='-'; buf[4]='\0'; }
        return;
    }

    /* 整数和小数分离 (纯整数运算) */
    {
        uint32_t scaled = (uint32_t)(val * (float)scale + 0.5f);
        int_part = scaled / scale;
        frac_part = scaled % scale;
    }

    /* 负号 */
    if (is_neg && pos < buf_len - 1) buf[pos++] = '-';

    /* 整数部分 */
    itoa_local((int)int_part, tmp);
    tmp_len = (uint8_t)strlen(tmp);
    if (pos + tmp_len < buf_len) {
        memcpy(&buf[pos], tmp, tmp_len);
        pos += tmp_len;
    }

    /* 小数部分 */
    if (decimals > 0 && pos < buf_len - 1) {
        buf[pos++] = '.';

        /* 前导零补齐 */
        {
            uint8_t d;
            for (d = 0; d < decimals; d++) {
                uint32_t p = s_pow10[decimals - 1 - d];
                if (pos < buf_len - 1)
                    buf[pos++] = (char)('0' + (int)((frac_part / p) % 10u));
            }
        }
    }

    buf[pos] = '\0';
}
