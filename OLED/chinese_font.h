/**
 * @file    chinese_font.h
 * @brief   16x16 中文字模模块 — 公共 API
 * @note    基于现有 afiskon/ssd1306 驱动，使用 ssd1306_DrawBitmap() 渲染
 *
 * 编码约定:
 *   - 字符串中字节 0x80~0xFF 表示中文字符，索引 = byte - 0x80
 *   - 字节 0x20~0x7E 为标准 ASCII
 *   - 字节 0x00 为字符串结束符
 *   - 使用 CH(索引) 宏在字符串中嵌入中文字符
 *
 * 布局 (128x64 OLED):
 *   - 16x16 中文字符，每行 16 像素高
 *   - 标题栏 16px + 3 个列表项 × 16px = 64px
 */
#ifndef __CHINESE_FONT_H
#define __CHINESE_FONT_H

#include "ssd1306.h"

/* ===== 字符索引定义 ===== */
/* 每个中文字符分配唯一索引 (0~127)，用于在字符串中编码 */

/* 菜单通用 */
#define CHI_ZHU     0   /* 主 */
#define CHI_CAI     1   /* 菜 */
#define CHI_DAN     2   /* 单 */
#define CHI_YUN     3   /* 运 */
#define CHI_XING    4   /* 行 */
#define CHI_XIAN    5   /* 显 */
#define CHI_SHI_Q   6   /* 示 */

/* 流量相关 */
#define CHI_SHUN    7   /* 瞬 */
#define CHI_SHI_H   8   /* 时 */
#define CHI_LIU     9   /* 流 */
#define CHI_LIANG   10  /* 量 */
#define CHI_LEI     11  /* 累 */
#define CHI_JI_Q    12  /* 计 */
#define CHI_ZONG    13  /* 总 */
#define CHI_SU      14  /* 速 */
#define CHI_PIN     15  /* 频 */
#define CHI_LV_Q    16  /* 率 */
#define CHI_DIAN    17  /* 电 */
#define CHI_WEN     18  /* 温 */
#define CHI_DU      19  /* 度 */
#define CHI_YA      20  /* 压 */
#define CHI_LI      21  /* 力 */
#define CHI_BANG    22  /* 棒 */
#define CHI_TU      23  /* 图 */
#define CHI_QU      24  /* 趋 */
#define CHI_SHI_V   25  /* 势 */

/* 参数设置 */
#define CHI_CAN     26  /* 参 */
#define CHI_SHU     27  /* 数 */
#define CHI_SHE     28  /* 设 */
#define CHI_ZHI     29  /* 置 */
#define CHI_JI_QZ   30  /* 基 */
#define CHI_BEN     31  /* 本 */
#define CHI_BIAO_Q  32  /* 标 */
#define CHI_KUANG   33  /* 况 */
#define CHI_YI      34  /* 仪 */
#define CHI_BIAO_V  35  /* 表 */
#define CHI_XI      36  /* 系 */
#define CHI_JIE     37  /* 介 */
#define CHI_ZHI_Q   38  /* 质 */
#define CHI_GONG    39  /* 工 */
#define CHI_WEI     40  /* 位 */
#define CHI_XIAO    41  /* 小 */
#define CHI_XIN     42  /* 信 */
#define CHI_HAO     43  /* 号 */
#define CHI_QIE     44  /* 切 */
#define CHI_CHU     45  /* 除 */
#define CHI_LV      46  /* 滤 */
#define CHI_BO      47  /* 波 */
#define CHI_ZU      48  /* 阻 */
#define CHI_NI      49  /* 尼 */
#define CHI_JIAN    50  /* 间 */

/* 输出/工况 */
#define CHI_SHU_V   51  /* 输 */
#define CHI_CHU_V   52  /* 出 */
#define CHI_MAI     53  /* 脉 */
#define CHI_CHONG   54  /* 冲 */
#define CHI_DANG    55  /* 当 */
#define CHI_MI      56  /* 密 */
#define CHI_DU_V    57  /* 密度(重复度用索引19) */

/* 管道/气体 */
#define CHI_GUAN    58  /* 管 */
#define CHI_DAO     59  /* 道 */
#define CHI_NEI     60  /* 内 */
#define CHI_JING    61  /* 径 */
#define CHI_QI      62  /* 气 */
#define CHI_TI      63  /* 体 */

/* 雷诺/修正 */
#define CHI_LEI_V   64  /* 雷 */
#define CHI_NUO     65  /* 诺 */
#define CHI_XIU     66  /* 修 */
#define CHI_ZHENG   67  /* 正 */
#define CHI_QI_V    68  /* 器 */
#define CHI_JI_J    69  /* 积 */

/* 累积管理 */
#define CHI_FAN     70  /* 反 */
#define CHI_XIANG   71  /* 向 */
#define CHI_JING_V  72  /* 净 */
#define CHI_QING    73  /* 清 */
#define CHI_LING    74  /* 零 */
#define CHI_YU      75  /* 预 */
#define CHI_ZHI_V   76  /* 值 */

/* 校准/量程 */
#define CHI_JIAO    77  /* 校 */
#define CHI_ZHUN    78  /* 准 */
#define CHI_DIAN_V  79  /* 点 */
#define CHI_MAN     80  /* 满 */
#define CHI_CHENG   81  /* 程 */

/* 系统 */
#define CHI_TONG    82  /* 统 */
#define CHI_HUI     83  /* 恢 */
#define CHI_FU      84  /* 复 */
#define CHI_CHANG   85  /* 厂 */
#define CHI_TONG_V  86  /* 通 */
#define CHI_XUN     87  /* 讯 */
#define CHI_DI      88  /* 地 */
#define CHI_ZHI_VV  89  /* 址 */
#define CHI_TE      90  /* 特 */
#define CHI_BEI     91  /* 备 */
#define CHI_XI_V    92  /* 息 */
#define CHI_MA      93  /* 码 */

/* 交互提示 */
#define CHI_CUO     94  /* 错 */
#define CHI_WU      95  /* 误 */
#define CHI_FAN_V   96  /* 范 */
#define CHI_WEI_V   97  /* 围 */
#define CHI_ZHI_QQ  98  /* 只 */
#define CHI_DU_Q    99  /* 读 */
#define CHI_QUE     100 /* 确 */
#define CHI_REN     101 /* 认 */
#define CHI_QU_V    102 /* 取 */
#define CHI_XIAO_V  103 /* 消 */
#define CHI_SUO     104 /* 所 */
#define CHI_YOU     105 /* 有 */
#define CHI_JIANG   106 /* 将 */
#define CHI_BEI_V   107 /* 被 */
#define CHI_MO      108 /* 默 */
#define CHI_DUI     109 /* 对 */

/* 字符总数 */
#define CHI_COUNT   110

/* ===== 编码宏 ===== */
/** 在字符串字面量中嵌入中文字符: CH(CHI_ZHU) → 0x80 */
#define CH(idx) ((char)(0x80 | (idx)))

/* ===== 公共 API ===== */

/**
 * @brief  绘制单个 16x16 中文字符
 * @param  x     列坐标 (0~111, 需留 16px 宽度)
 * @param  y     行坐标 (0~48, 需留 16px 高度)
 * @param  idx   字符索引 (0~CHI_COUNT-1)
 * @param  color 像素颜色
 */
void ssd1306_DrawChinese(uint8_t x, uint8_t y, uint8_t idx, SSD1306_COLOR color);

/**
 * @brief  绘制混合中英文字符串
 * @note   ASCII (0x20~0x7E) 使用 Font_6x8 居中在 16px 行高内
 *         中文 (0x80~0xFF) 使用 16x16 点阵
 * @param  x     起始列
 * @param  y     起始行
 * @param  str   混合编码字符串 (以 CH() 宏嵌入中文)
 * @param  color 像素颜色
 * @return 字符串总像素宽度
 */
uint8_t ssd1306_WriteMixedStr(uint8_t x, uint8_t y, const char *str, SSD1306_COLOR color);

/**
 * @brief  绘制纯中文字符串
 * @note   每个字符 16px 宽，使用 16x16 点阵
 * @param  x     起始列
 * @param  y     起始行
 * @param  str   中文编码字符串 (字节 0x80~0xFF 为字符索引)
 * @param  color 像素颜色
 * @return 字符串总像素宽度
 */
uint8_t ssd1306_WriteChineseStr(uint8_t x, uint8_t y, const char *str, SSD1306_COLOR color);

/**
 * @brief  计算混合字符串像素宽度
 * @param  str   混合编码字符串
 * @return 总像素宽度
 */
uint8_t ssd1306_MixedStrWidth(const char *str);

#endif /* __CHINESE_FONT_H */
