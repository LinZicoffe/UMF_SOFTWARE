# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

UMF 超声波流量传感器嵌入式固件，基于 STM32F103C8T6 (ARM Cortex-M3, 72MHz, **64KB Flash, 20KB RAM**)。
通过 USART1 与 UFL-1A 超声波流量模组通信（自定义 BCD 协议），USART2 作为 Modbus RTU 从站（地址可配）。
支持 4~20mA DAC 输出（PWM 模拟）、OLED 显示（SSD1306 128×64, afiskon 库 + bit-bang SPI）、Flash 模拟 EEPROM 参数存储。

作者: liyongtai (nylyt)。所有注释和文档使用中文。

## 构建系统

- **IDE/工具链**: IAR Embedded Workbench for ARM (EWARM V8.32)
- **工程文件**: `EWARM/UMF.ewp`
- **工作空间**: `EWARM/Project.eww`
- **MCU 配置**: `UMF.ioc` (STM32CubeMX, 目标工具链 EWARM V8.32)
- **编译器定义**: `USE_HAL_DRIVER`, `STM32F103xB`
- **无 Makefile/CMakeLists.txt** — 仅通过 IAR IDE 或命令行构建
- **新增源文件必须手动添加到 `.ewp`** — IAR 不会自动发现新文件。新建 `.c` 后必须在 `EWARM/UMF.ewp` 中对应 `<group>` 节点下添加 `<file>` 条目。参考格式：
  ```xml
  <file>
      <name>$PROJ_DIR$\..\BSP\Src\new_module.c</name>
  </file>
  ```

无自动化测试框架。验证通过串口调试和 OLED 显示观察完成。

## 架构

裸机超级循环（无 RTOS）。数据流:

```
USART1 (DMA + IDLE中断) ← UFL-1A 超声波流量模组
  → BCD 解码 → 瞬时流量/温度/压力/累积流量
  → USART2 (Modbus RTU 从站, RS-485)
  → 4~20mA DAC 输出 (TIM1/TIM4 PWM)
  → SSD1306 OLED 显示 (afiskon 库 + bit-bang SPI)
```

### 主循环 (`Core/Src/main.c`)

1. **按键/菜单处理** — `key_get_event()` → `menu_process()` 分发到菜单或运行显示翻页
2. **运行显示刷新** — 200ms 周期 (`DisplayTimeBase >= 20`)，组装 `run_display_input_t` → `run_display_render()` → `ssd1306_UpdateScreen()`
3. **IWDG 看门狗刷新** — 重载值 999，约 1 秒超时
4. **UART1/UART2 通信处理**
5. **DAC PWM 输出** — `PWMConfig()` 配置 TIM1/TIM4
6. **DAC 线性换算** — 仪表系数×介质系数×流量 → `ConvertFunc()` → 小信号切除判定

### TIM3 中断回调 (`Core/Src/tim.c`)

10ms 周期中断 (`HAL_TIM_PeriodElapsedCallback`)：
- `key_scan_10ms()` — 按键消抖 + 组合键判定
- `menu_tick_10ms()` — 菜单空闲超时
- `DisplayTimeBase++` — 显示刷新计数
- `Timer3Uart1TimeBase10ms++` / `Timer3Uart2TimeBase10ms++` — UART 通信时序

### DMA 通道映射

| DMA 通道 | 外设 | 方向 |
|----------|------|------|
| DMA1_Channel4 | USART1_TX | 发送 |
| DMA1_Channel5 | USART1_RX | 接收 |
| DMA1_Channel6 | USART2_RX | 接收 |
| DMA1_Channel7 | USART2_TX | 发送 |

### NVIC 中断优先级

优先级分组: `NVIC_PRIORITYGROUP_2`（2 位抢占 + 2 位子优先级）

| 中断源 | 抢占优先级 | 子优先级 | 说明 |
|--------|-----------|---------|------|
| USART1 | 0 | 1 | 流量模组通信（最高优先级） |
| TIM3 | 1 | 0 | 10ms 系统定时 |
| USART2 | 1 | 2 | Modbus RTU 通信 |

## 硬件引脚关键信息

> 完整引脚分配见 README.md "硬件平台" 章节。

### 按键输入（命名反转，易踩坑）

| 引脚 | 代码宏名 | 实际面板功能 |
|------|----------|-------------|
| PC15 | K_MOV (K1) | 向下选择 (KEY_DOWN) |
| PA11 | K_ADD (K3) | 向上选择 (KEY_UP) |
| PA0 | K_SUB (K2) | 确认/进入 (KEY_ENTER) |

**组合键**: K1+K2 = KEY_BACK, K1+K2+K3 = KEY_HOME

> **注意**: CubeMX 引脚命名与面板接线相反 — `K_MOV` 实为向下键，`K_SUB` 实为确认键。

### 其他关键引脚

| 引脚 | 功能 | 备注 |
|------|------|------|
| PA1 | USART2_DE | RS-485 方向控制，发送前置高、发送后拉低 |
| PA8 | TIM1_CH1 | DAC 高字节 PWM（高级定时器，需 `__HAL_TIM_MOE_ENABLE()`） |
| PB6 | TIM4_CH1 | DAC 低字节 PWM |
| PB5 | 电源指示 LED | |

## 模块结构

### BSP 层 (`BSP/`)

| 文件 | 职责 |
|------|------|
| `bsp_usart.c/h` | USART1 流量模组通信（BCD 协议）+ USART2 Modbus RTU 从站（功能码 01/03/04/05/06/10） |
| `bsp_menu.c/h` | 菜单系统 — 5 层导航栈 + 6 种界面模式 + 两级密码门控，覆盖 S03~S44 共 42 屏幕 |
| `key.c/h` | 事件驱动按键驱动 — 10ms 扫描、消抖、组合键检测，返回 `key_event_t` |
| `param_storage.c/h` | 参数存储 — RAM 缓存 + Flash 持久化，getter/setter API，含七点标定参数 |
| `cal_table.c/h` | 七点流量标定 — 分段线性插值，Modbus 寄存器 95~123 读写标定系数和标定点百分比（**注意**: `.c` 在 `BSP/Src/`，其余 BSP 文件均在 `BSP/` 根目录） |
| `run_display.c/h` | 运行显示 — S01 主界面 + S02 辅助变量页，通过 `run_display_input_t` 接收 const 数据 |
| `eeprom.c/h` | Flash 模拟 EEPROM（底层读写，Page 54~63 参数存储） |
| `mystring.c/h` | 字符串工具函数（Int2String, insert_char） |
| `ftoa.c/h` | 轻量 float→string 转换，纯整数运算，替代 printf %f 节省 3~8KB Flash |

### OLED 层 (`OLED/`)

| 文件 | 职责 |
|------|------|
| `ssd1306.c/h` | **当前驱动** — afiskon/stm32-ssd1306 库，bit-bang SPI 适配 |
| `ssd1306_conf.h` | 硬件配置 — 引脚映射、字体选择、bit-bang SPI 标志 |
| `ssd1306_fonts.c/h` | 字体数据 — Font_6x8 + Font_11x18（Font_7x10 和 Font_16x26 已禁用节省 Flash） |
| `oled.c/h` | 旧版驱动（保留未删），已不参与编译 |
| `oledfont.h` | 旧版字体数据（保留未删） |
| `bmp.h` | 位图资源（温度度符号图标） |

### SSD1306 驱动 API 速查

```c
ssd1306_Init();                                    // 初始化
ssd1306_Fill(Black/White);                         // 填充
ssd1306_UpdateScreen();                            // 帧缓冲 → 硬件
ssd1306_DrawPixel(x, y, color);                   // 画点
ssd1306_WriteChar(ch, Font, color);                // 单字符
ssd1306_WriteString(str, Font, color);             // 字符串
ssd1306_SetCursor(x, y);                          // 设置光标
ssd1306_DrawBitmap(x, y, bmp, w, h, color);       // 位图
ssd1306_Line(x1, y1, x2, y2, color);              // 画线
ssd1306_DrawRectangle(x1, y1, x2, y2, color);     // 矩形
ssd1306_InvertRectangle(x1, y1, x2, y2);          // 反色矩形
ssd1306_SetContrast(value);                        // 对比度
```

可用字体: `Font_6x8`, `Font_11x18`

## 关键数据变量

### 传感器数据 (`bsp_usart.h`)

| 变量 | 类型 | 来源 | 含义 |
|------|------|------|------|
| `FlowRateValue` | `Uart_SendfloatTypeDef` (union) | UART1 BCD | 瞬时流量 |
| `FlowTemperature` | `Uart_SendfloatTypeDef` (union) | UART1 BCD | 温度 |
| `FlowPressure` | `Uart_SendfloatTypeDef` (union) | UART1 BCD | 压力 |
| `Cumulativeflow` | `uint64_t` | 累加计算 | 累积流量 |
| `strFlowSumBuf` | `unsigned char[20]` | BCD 转字符串 | 累积流量字符串 |
| `ModuleState` | `uint8_t` | UART 状态 | 0=ok, 非0=Err |

### DAC/校准数据 (`main.h`)

| 变量 | 类型 | 说明 |
|------|------|------|
| `DacValueBuf[2]` | `uint16_t[2]` | DAC 零点/满度值 (DacZeroValue/DacFullValue) |
| `DacValue` | `uint16_t` | 当前 DAC 输出值 |
| `SpanValueBuf[2]` | `SpanTypeDef[2]` | 4mA/20mA 量程映射值 (SpanLoValue/SpanHiValue) |
| `BitControlBuf[50]` | `uint16_t[50]` | Modbus 位控制寄存器 |

### 时间基准 (`tim.c`, volatile)

| 变量 | 类型 | 说明 |
|------|------|------|
| `DisplayTimeBase` | `uint8_t` | 显示刷新计数器（>= 20 时触发 200ms 刷新） |
| `Timer3Uart1TimeBase10ms` | `uint8_t` | UART1 通信时序 |
| `Timer3Uart2TimeBase10ms` | `uint8_t` | UART2 通信时序 |

## EEPROM 存储映射

| Flash 页 | 地址 | 用途 |
|----------|------|------|
| Page 54 | `0x0800D800` | OLED 抗干扰自愈间隔 (oled_recovery_interval) |
| Page 55 | `0x0800DC00` | 信号处理组 (小信号切除/滤波/阻尼) |
| Page 56 | `0x0800E000` | 输出配置组 (频率/脉冲当量/语言) |
| Page 57 | `0x0800E400` | 介质工况组 (密度/管径/气压/气温/雷诺) |
| Page 58 | `0x0800E800` | 系统/累积组 (地址/波特率/总量系数/预设) |
| Page 59 | `0x0800EC00` | DAC 校准值 (DacZero, DacFull: uint16_t × 2) |
| Page 60 | `0x0800F000` | 基本参数 (标况/流量单位/累积单位) |
| Page 61 | `0x0800F400` | 仪表系数 + 七点标定合并组 (Len=16: meter_coeff, cal_enabled, cal_k[7], cal_pct[7]) |
| Page 62 | `0x0800F800` | 介质系数 (medium_coeff) |
| Page 63 | `0x0800FC00` | Span 量程 (SpanLo, SpanHi: uint32_t × 2) |

**注意**: ICF 链接器将代码区限制在 Page 0~53 (54KB)，Page 54~63 (10KB) 严格保留为 Flash-EEPROM 参数存储区，互不干扰。写入前确认地址范围。

## 菜单系统架构

菜单由 `bsp_menu` 模块管理，采用导航栈架构（最大 5 层），6 种界面模式：

| 模式 | 用途 | 交互 |
|------|------|------|
| M1 列表 (LIST) | 菜单导航 | KEY_UP/DOWN 移动, KEY_ENTER 进入, KEY_BACK 返回 |
| M2 数值 (NUMERIC) | 参数编辑 | KEY_UP +step, KEY_DOWN -step, KEY_ENTER 保存, KEY_BACK 取消 |
| M3 枚举 (ENUM) | 选项切换 | KEY_UP/DOWN 切换, KEY_ENTER 确认 |
| M4 只读 (READONLY) | 数据查看 | 任意键返回 |
| M5 确认 (CONFIRM) | 危险操作 | KEY_UP/DOWN YES/NO, KEY_ENTER 执行 |
| M6 密码 (PASSWORD) | 身份验证 | KEY_UP/DOWN 改数字, KEY_ENTER 下一位 |

密码两级门控: 普通用户 `000`，工程师/开发者 `123`。普通用户通过 OLED 设置 `meter_coeff` 和 `medium_coeff` 时限制为 0.800~1.200，工程师/开发者可使用完整设备范围。菜单通过 `param_storage` getter/setter 读写参数。

### 菜单导航结构

```
S03 主菜单 (5 项)
  ├── 1.Display      → 返回运行显示
  ├── 2.Parameter    → S04 密码 → S05 基本设置 (11 项)
  ├── 3.Totalizer    → S04 密码 → S28 累计总量管理 (5 项)
  ├── 4.Calibration  → S04 密码 → S34 校准 (4 项)
  └── 5.System       → S04 密码 → S39 系统设置 (4 项)
```

详细屏幕规格见 `UMF_HMI_Screen_Design.md`。

## 信号链处理流水线

```
UFL-1A BCD 原始流量
  → 累加器去极值滤波 (N=10, K=1, 扣除最大值取均值)
  → × 仪表系数 (meter_coeff)
  → × 介质系数 (medium_coeff)
  → × 七点标定分段线性插值 (cal_table, 可选)
  → 小信号切除判定 (< 量程下限 + 量程×N% 时输出零点)
  → DAC 线性插值 ConvertFunc() → clamp [DacZero, DacFull]
  → TIM1/TIM4 PWM 输出
```

- **滤波**: `flow_filter_feed()` 为 `bsp_usart.c` 内 static 函数（非独立模块文件），在 `Uart1_Receive_Function()` BCD 解析后调用；`effective_flow_rate()` 为 public API，优先返回滤波值，未就绪时回退原始值
- **标定**: `cal_table` 模块，7 个标定点默认百分比 [0, 3, 10, 25, 50, 75, 100]，修正系数 k[0..6] 范围 0.5~2.0
- **模拟模式**: Modbus 寄存器 40049=1 时，模拟流量/温度/累积值替代真实传感器数据（仅 RAM，掉电重置）

## BCD 协议基础

USART1 与 UFL-1A 通信，自定义 BCD 编码：
- **帧最小长度**: 28 字节（<28 直接丢弃）
- **帧类型**: `0x0b`（瞬时流量）等，`0x0b` 帧从当前帧 BCD 数据计算（非残留值）
- **解析变量**: `FlowRateValue`（瞬时流量）、`FlowTemperature`、`FlowPressure` — 均为 `Uart_SendfloatTypeDef` union
- **接收**: DMA + IDLE 中断，缓冲区 `UART_RX_LEN=150`

## Modbus 寄存器映射 (USART2, 地址可配)

| 地址 | 功能 | 数据类型 | 读写 |
|------|------|----------|------|
| 40001~40002 | 瞬时流量 | float | FC03 |
| 40003~40004 | 温度 | float | FC03 |
| 40005~40006 | 压力 | float | FC03 |
| 40021~40022 | DAC 零点/满度 | uint16 | FC03/FC10 |
| 40023~40024 | 流量单位/累积单位 | uint16 | FC03/FC06 |
| 40025~40028 | 仪表系数/介质系数 | float | FC03/FC06 |
| 40029~40030 | 小信号切除 | float | FC03/FC06 |
| 40031~40032 | 量程低/高值 | float | FC03/FC10 |
| 40041~40044 | 累积流量 | uint64 | FC03 |
| 40049 | 模拟总开关 | uint16 | FC03/FC06 |
| 40051~40056 | 模拟流量/温度/累积 | float | FC03/FC06 |
| 40061 | 通信状态 ModuleState | uint16 | FC03 |
| 40062~40067 | 正向/反向/净累积 | float | FC03 |
| 40068~40069 | 实时 4-20mA 电流 | float | FC03 |
| 40070~40091 | 扩展配置参数 | uint16/float | FC03/FC06/FC10 |
| 40092 | 通信地址 | uint16 | FC03/FC06 |
| 40093 | 波特率 | uint16 | FC03/FC06 |
| 40094 | 语言 | uint16 | FC03/FC06 |
| 40095 | OLED 自愈间隔 (×100ms) | uint16 | FC03/FC06 |
| 40096 | 标定使能 | uint16 | FC03/FC06 |
| 40097~40109 | 标定修正系数 k[0..6] | float | FC03/FC06 |
| 40110~40123 | 标定点百分比 pct[0..6] | float | FC03/FC06 |

> **float 参数**: 占 2 个连续寄存器，FC06 分次写入时低位字先缓存、高位字到达后触发 setter。
> **地址/波特率**: FC06 写入后立即（地址）或延迟（波特率）生效并持久化到 Flash。上位机需切换到新参数才能继续通信。
> **扩展参数详情**: 寄存器 40070~40091 包括标准工况、滤波、阻尼、频率、脉冲当量、密度、管径、气压、气温、雷诺、累积系数、预设总量。详见 README.md 完整表。

## 资源预算

| 资源 | 总量 | 已用 | 剩余 |
|------|------|------|------|
| Flash (代码区) | 54KB (Page 0~53) | ~34KB | ~20KB |
| Flash (EEPROM) | 10KB (Page 54~63) | 参数存储 | — |
| RAM | 20KB | ~7KB | ~13KB |

## 模块设计原则（强制）

所有新模块、重构模块必须严格遵循以下架构约束：

### 1. 文件结构

- **头文件 (.h)** 仅声明对外接口（public API），不暴露任何内部实现细节
- **源文件 (.c)** 包含全部内部实现，内部变量和内部函数一律用 `static` 修饰
- 头文件中不得定义内部缓冲区、内部状态变量或内部辅助函数

```
module.h  → 只有: typedef、public 函数声明、public 宏/常量
module.c  → 包含: static 变量、static 函数、public 函数实现
```

### 2. INPUT / OUTPUT 接口分离

模块间交互 **只能** 通过明确的 INPUT 和 OUTPUT 参数完成，禁止直接读写其他模块的内部变量：

- **INPUT**: 以 `const` 指针或值传递方式传入模块的数据
- **OUTPUT**: 通过非 const 指针传出模块的计算结果
- 模块内部所有状态变量必须为 `static`，外部不可直接访问

```c
// 正确：通过接口交互
HAL_StatusTypeDef module_process(const float *p_input,   // INPUT
                                  uint32_t input_len,     // INPUT
                                  module_result_t *p_result); // OUTPUT

// 禁止：外部直接读写 static 变量
// extern float g_internal_state;  ← 不允许
```

### 3. OUTPUT 设计选择

| 场景 | OUTPUT 方式 | 示例 |
|------|------------|------|
| 单一数值 | 直接用 `float *` 指针 | `module_get_value(float *p_val)` |
| 多个相关输出 | 使用结构体指针 | `module_process(..., result_t *p_out)` |
| 状态/诊断信息 | 独立诊断结构体或 getter 函数 | `module_get_status(status_t *p_status)` |

结构体按职责分组，避免"上帝结构体"。

### 4. 参数读写接口

所有可调参数必须通过专用 API 读写，不得直接暴露变量：

```c
void  module_set_param(float value);
float module_get_param(void);
HAL_StatusTypeDef module_config(const module_config_t *p_cfg);
```

### 5. 典型模块模板

```c
// === module.h ===
#ifndef MODULE_H
#define MODULE_H
#include "stm32f1xx_hal.h"

typedef struct { /* 配置参数 */ } module_config_t;
typedef struct { /* 核心输出 */ } module_result_t;

void              module_init(const module_config_t *p_cfg);
HAL_StatusTypeDef module_process(const float *p_in, uint32_t len, module_result_t *p_out);
void              module_reset(void);

#endif

// === module.c ===
#include "module.h"
static float s_internal_buf[BUFFER_SIZE];  // static 内部缓冲
static float s_param_value;                 // static 可调参数

static float do_internal_calc(float x) { ... }  // static 内部函数

HAL_StatusTypeDef module_process(const float *p_in, uint32_t len,
                                  module_result_t *p_out) {
    // 使用 p_in (INPUT) -> 处理 -> 写 p_out (OUTPUT)
}
```

### 6. ISR 安全规则

- ISR 中只设置标志位/递增计数器、调用轻量级 10ms 扫描函数，不执行复杂逻辑
- 主循环中根据标志位处理业务逻辑
- ISR 与主循环共享的变量必须声明为 `volatile`
- 按键驱动只返回事件码，不直接调用菜单/显示函数
- 禁止在 ISR 中调用 `OLED_Refresh()`、`ssd1306_UpdateScreen()`、`HAL_FLASH_xxx()` 等耗时操作

## 代码规范

- 语言: C (C99 兼容)，所有注释使用中文
- 类型: 使用 `float` 进行浮点运算（Cortex-M3 无 FPU，使用软浮点）
- 状态码: `HAL_StatusTypeDef` (HAL_OK / HAL_ERROR / HAL_BUSY / HAL_TIMEOUT)
- STM32CubeMX 生成的 `Core/` 目录代码使用 `/* USER CODE BEGIN/END */` 保护块 — 仅在保护块内修改
- static 变量命名使用 `s_` 前缀
- 头文件包含顺序: (1) 标准 C 库 → (2) STM32 HAL → (3) 项目 BSP → (4) 应用层
- 禁止动态内存分配（无 malloc/free），大数组定义为全局变量

## IAR EWARM 编译常见问题与修复指南

> 本节记录实际开发中遇到的编译错误/警告，以及通用的排查和修复方法。

### 1. Error[Li006]: 重复定义 (duplicate definitions)

**症状**: 链接阶段报 `duplicate definitions for "XXX"` 在两个 `.o` 文件中。

**根本原因**: `.h` 文件中直接定义了非 `static` 的全局变量或数组。当该头文件被多个 `.c` 文件 `#include` 时，每个编译单元都会生成一个同名全局符号，链接器发现冲突。

**典型案例**:
```c
// bmp.h (错误写法)
const unsigned char BMP[] = {0x00, 0x0E, 0x0A};  // 每个包含此头文件的 .c 都会生成一个 BMP
```

**修复方法**:

| 方案 | 适用场景 | 做法 |
|------|----------|------|
| `static` 修饰 | 小型只读数据（如位图、查找表），每个编译单元各自持有一份副本 | `static const unsigned char BMP[] = {...};` |
| `extern` + 单独定义 | 大型数据，需要全程序只有一份 | `.h` 中 `extern const unsigned char BMP[];`，在某个 `.c` 中 `const unsigned char BMP[] = {...};` |

**通用规则**: `.h` 文件中禁止出现非 `static` 的变量/数组**定义**（`= {...}` 或 `= value`）。只能有声明（`extern ...;`）或 `static` 定义。

### 2. Error[Pa165]: 声明不一致 (incompatible declarations)

**症状**: 编译器报某变量在两处声明类型不同，如 `"uint8_t X"` vs `"uint8_t volatile X"`。

**根本原因**: 变量在 `.c` 中定义时使用了 `volatile`（或其他类型修饰符），但对应的 `.h` 中 `extern` 声明没有同步修改。

**修复**: `.h` 中的 `extern` 声明必须与 `.c` 中的**定义**完全匹配，包括 `volatile`、`const` 等修饰符。

```c
// tim.h
extern volatile uint8_t DisplayTimeBase;  // 必须与 tim.c 中的定义一致

// tim.c
volatile uint8_t DisplayTimeBase;         // 定义
```

### 3. Warning[Pe188]: 枚举类型混用

**修复**: 添加显式类型转换 `(enum_type_t)`。

```c
// 警告写法
s_current_page = (s_current_page + 1) % RUN_PAGE_COUNT;

// 正确写法
s_current_page = (run_page_t)((s_current_page + 1) % RUN_PAGE_COUNT);
```

### 4. Warning[Pe223]: 隐式函数声明

**修复**: 在调用处所在 `.c` 文件顶部 `#include` 对应的头文件。

### 5. Warning[Pe550]: 变量赋值后未使用

**修复**:
- 确实不需要 → 删除变量
- 预留将来使用 → 用 `(void)var;` 消除警告
- 函数参数未使用 → 用 `(void)param;` 在函数体内消除

### 通用排查流程

```
1. Error[Li006]  → 检查 .h 中是否有非 static 的变量定义
2. Error[Pa165]  → 对比 .h extern 声明与 .c 定义是否完全一致（含 volatile/const）
3. Warning[Pe188]→ 添加显式 (enum_t) 类型转换
4. Warning[Pe223]→ 添加 #include 对应头文件
5. Warning[Pe550]→ 删除未使用变量，或用 (void) 消除
```

## 已安装 Skill

项目已安装以下嵌入式开发 Skill（`.claude/skills/`），通过自然语言或斜杠命令调用。详细参数请用 `--help` 查看。

| Skill | 触发方式 | 依赖 | 说明 |
|-------|---------|------|------|
| `build-iar` | `/build-iar` 或 "用 IAR 编译" | 无 | IAR 命令行编译，工程文件 `EWARM/UMF.ewp` |
| `stm32-hal-development` | `/stm32-hal-development` | 无 | HAL 开发指导、BSP 模板、外设最佳实践 |
| `peripheral-driver` | `/peripheral-driver` | 无 | 外设驱动搜索/适配/脚手架生成 |
| `modbus-debug` | `/modbus-debug` | `pymodbus, pyserial` | Modbus RTU/TCP 读写、扫描从站 |
| `serial-monitor` | `/serial-monitor` | `pyserial` | 串口监视、抓包、自动复位 |
| `workflow` | `/workflow` 或 "编译烧录" | 无 | 编译→烧录→监控流水线 |

> **注意**: IAR 编译仅限 Windows 环境。

## Git 与文档管理规则

- **项目文档**: `README.md` 为项目主文档，包含功能描述、构建方法、文件结构、版本历史等
- **推送前必须更新 `README.md`**: 每次向 GitHub 推送前，必须先更新 `README.md` 中的版本日志和变更摘要，确保文档与代码同步
- **注释语言**: 所有注释和文档使用中文
