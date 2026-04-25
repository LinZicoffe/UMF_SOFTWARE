#!/usr/bin/env python3
"""
UMF_SOFTWARE 16x16 中文字模生成脚本

功能:
  - 从 GB2312 字库提取指定汉字的 16x16 点阵
  - 输出 C 数组格式 (列行式, 逐列顺向, 高位在上)
  - 自动生成 chinese_font_data.h 可直接拷贝到 chinese_font.c

使用:
  python generate_chinese_font.py

依赖:
  - Python 3.6+
  - 无需额外依赖 (使用内置 bytes 操作)

字模格式说明:
  - 每个汉字 16x16 = 32 字节
  - 前 16 字节: 上半部分 (y=0~7)
  - 后 16 字节: 下半部分 (y=8~15)
  - 每字节表示一列的 8 个像素, 高位在上
"""

import struct

# ===== 汉字列表 (与 chinese_font.h 中索引对应) =====
CHAR_LIST = [
    # 0-6: 菜单通用
    "主", "菜", "单", "运", "行", "显", "示",
    # 7-25: 流量相关
    "瞬", "时", "流", "量", "累", "计", "总", "速", "频", "率",
    "电", "温", "度", "压", "力", "棒", "图", "趋", "势",
    # 26-50: 参数设置
    "参", "数", "设", "置", "基", "本", "标", "况",
    "仪", "表", "系", "介", "质", "工", "位",
    "小", "信", "号", "切", "除", "滤", "波", "阻", "尼", "间",
    # 51-57: 输出/工况
    "输", "出", "脉", "冲", "当", "密",
    # 58-63: 管道/气体
    "管", "道", "内", "径", "气", "体",
    # 64-69: 雷诺/修正
    "雷", "诺", "修", "正", "器", "积",
    # 70-76: 累积管理
    "反", "向", "净", "清", "零", "预", "值",
    # 77-81: 校准/量程
    "校", "准", "点", "满", "程",
    # 82-93: 系统
    "统", "恢", "复", "厂", "通", "讯", "地", "址", "特", "备", "息", "码",
    # 94-109: 交互提示
    "错", "误", "范", "围", "只", "读", "确", "认", "取", "消", "所", "有",
    "将", "被", "默", "对",
]

# ===== GB2312 16x16 点阵字库 =====
# 使用 Hzk16 字库格式: 每个汉字 32 字节, 逐列式
# 字库起始区位: 1区 = ASCII + 符号, 16-87区 = 汉字
# 汉字编码: 区位码 = (GB2312高字节-0xA0) * 100 + (低字节-0xA0)
# 字库偏移: (区号-1)*94*32 + (位号-1)*32

def get_hz_offset(char):
    """计算汉字在 Hzk16 字库中的偏移"""
    try:
        gb_bytes = char.encode('gb2312')
        if len(gb_bytes) != 2:
            return None
        # 区位码计算
        qu = gb_bytes[0] - 0xA0  # 区号 (16-87)
        wei = gb_bytes[1] - 0xA0  # 位号 (1-94)
        # Hzk16 偏移 (从第1区开始, 汉字从16区开始)
        offset = (qu - 1) * 94 * 32 + (wei - 1) * 32
        return offset
    except:
        return None

def load_hzk16(filepath="HZK16"):
    """加载 Hzk16 字库文件"""
    try:
        with open(filepath, 'rb') as f:
            return f.read()
    except FileNotFoundError:
        print(f"警告: 未找到字库文件 {filepath}")
        print("请从以下地址下载 Hzk16 字库:")
        print("  https://github.com/aguegu/DotMatrixForLCD/blob/master/resource/HZK16")
        return None

def get_font_data(hzk16, char):
    """从 Hzk16 提取单个汉字的 32 字节字模"""
    offset = get_hz_offset(char)
    if offset is None or hzk16 is None:
        # 返回空字模 (全零)
        return bytes(32)
    return hzk16[offset:offset+32]

def format_c_array(char_idx, char, font_data):
    """格式化为 C 数组元素"""
    lines = []
    lines.append(f"/* {char_idx}: {char} */")
    lines.append("{")
    # 8 字节一行
    for row in range(4):
        start = row * 8
        vals = [f"0x{font_data[i]:02X}" for i in range(start, start+8)]
        lines.append("    " + ",".join(vals) + ",")
    lines.append("},")
    return "\n".join(lines)

def generate_font_header():
    """生成完整的 C 字模数组"""
    hzk16 = load_hzk16()

    output = []
    output.append("/* ===== 自动生成的 16x16 中文字模数据 ===== */")
    output.append("/* 取模格式: Hzk16 列行式, 逐列顺向, 高位在上 */")
    output.append("/* 每字 32 字节: 前16字节上半(y=0~7), 后16字节下半(y=8~15) */")
    output.append("")
    output.append("static const unsigned char s_chinese_font[CHI_COUNT][32] = {")

    for idx, char in enumerate(CHAR_LIST):
        font_data = get_font_data(hzk16, char)
        output.append(format_c_array(idx, char, font_data))

    output.append("};")

    return "\n".join(output)

def main():
    print("=" * 60)
    print("UMF_SOFTWARE 16x16 中文字模生成器")
    print("=" * 60)
    print()

    header_content = generate_font_header()

    # 输出到文件
    output_file = "chinese_font_data.h"
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(header_content)

    print(f"生成完成: {output_file}")
    print()
    print("使用方法:")
    print("  1. 将 chinese_font_data.h 内容替换到 chinese_font.c")
    print("     的 s_chinese_font 数组定义中")
    print()
    print("  2. 如需添加新汉字, 编辑 CHAR_LIST 并重新运行脚本")
    print()

    # 如果没有字库文件, 提供在线取模工具说明
    print("=" * 60)
    print("备选方案: 使用在线取模工具")
    print("=" * 60)
    print()
    print("  波特律动 LED 取模工具: https://led.baud-dance.com")
    print("  取模设置:")
    print("    - 点阵大小: 16x16")
    print("    - 取模方式: 逐列式")
    print("    - 扫描方式: 顺向 (从上到下)")
    print("    - 输出格式: 十六进制, C语言")
    print("    - 高位在前: 是")
    print()

    # 显示前几个汉字的字模格式示例
    print("字模格式示例 (主):")
    print("  每字 32 字节, 排列: 第0列(2字节) → 第1列(2字节) → ...")
    print("  {")
    print("    0x10,0x00,  /* 第0列: 上半=0x10(第4位亮), 下半=0x00 */")
    print("    0x10,0x00,  /* 第1列 */")
    print("    ...")
    print("  }")

if __name__ == "__main__":
    main()