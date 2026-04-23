# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

UMF 超声波流量传感器嵌入式固件，基于 STM32F103C8T6 (ARM Cortex-M3, 72MHz, **64KB Flash, 20KB RAM**)。
通过 USART1 与 UFL-1A 超声波流量模组通信（自定义 BCD 协议），USART2 作为 Modbus RTU 从站（地址 2）。
支持 4~20mA DAC 输出（PWM 模拟）、OLED 显示（SSD1306 128×64）、Flash 模拟 EEPROM 参数存储。

作者: liyongtai (nylyt)。所有注释和文档使用中文。

## 构建系统

- **IDE/工具链**: IAR Embedded Workbench for ARM (EWARM V8.32)
- **工程文件**: `EWARM/UMF.ewp`
- **工作空间**: `EWARM/Project.eww`
- **启动文件**: `EWARM/startup_stm32f103xb.s`
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
  → OLED 显示 (SPI bit-bang)
```

### 主循环 (`Core/Src/main.c`)

1. OLED 显示刷新（200ms 周期，`DisplayTimeBase >= 20`）
2. IWDG 看门狗刷新
3. UART1 通信处理
4. UART2 通信处理
5. DAC PWM 输出
6. DAC 值线性换算（`ConvertFunc`）
7. 按键/菜单处理（`keyFunc`）

### TIM3 中断回调 (`Core/Src/tim.c`)

10ms 周期中断，回调中递增所有时间基准：
- `DisplayTimeBase` — 显示刷新计数
- `Timer3Uart1TimeBase10ms` — UART1 通信时序
- `Timer3Uart2TimeBase10ms` — UART2 通信时序
- `keysetTimeBase` / `keyaddTimeBase` / `keysubTimeBase` — 按键计时
- `keyadd10TimeBase` / `keysub10TimeBase` — 按键加速计时
- **直接调用 `keyscan()`**（存在 ISR 与主循环的竞态风险）

## 硬件引脚分配

### OLED 显示屏 (SPI bit-bang)

| 引脚 | 宏名 | 功能 |
|------|------|------|
| PB0 | `OLED_CLK` | SPI 时钟 (SCL) |
| PA4 | `OLED_SDA` | SPI 数据 (MOSI/SDA) |
| PA5 | `OLED_RES` | 硬件复位 |
| PA6 | `OLED_DC` | 数据/命令选择 |
| PA7 | `OLED_CS` | 片选 |

### 按键输入

| 引脚 | 宏名 | 功能 |
|------|------|------|
| PC15 | `K_MOV` | 确认/设置键 (Key_Set in code) |
| PA11 | `K_ADD` | 增加键 |
| PA0 | `K_SUB` | 减少键 (Key_Sub in code) |

**注意**: `key.h` 中的宏名与引脚名存在交叉映射：
```c
#define Key_Sub (!(HAL_GPIO_ReadPin(K_MOV_GPIO_Port, K_MOV_Pin)))   // PC15 = 确认键
#define Key_Add (!(HAL_GPIO_ReadPin(K_ADD_GPIO_Port, K_ADD_Pin)))   // PA11 = 增加键
#define Key_Set (!(HAL_GPIO_ReadPin(K_SUB_GPIO_Port, K_SUB_Pin)))   // PA0 = 减少键
```

### 串口通信

| 引脚 | 功能 | 说明 |
|------|------|------|
| PA9 | USART1_TX | 流量模组通信 |
| PA10 | USART1_RX | 流量模组通信 |
| PA2 | USART2_TX | Modbus RTU 从站 |
| PA3 | USART2_RX | Modbus RTU 从站 |
| PA1 | USART2_DE | RS-485 方向控制 |

### DAC 输出 (PWM)

| 引脚 | 定时器 | 说明 |
|------|--------|------|
| PA8 | TIM1_CH1 | DAC 高字节输出 |
| PB6 | TIM4_CH1 | DAC 低字节输出 |

### 其他

| 引脚 | 功能 |
|------|------|
| PB5 | 电源指示 LED |

## 模块结构

### BSP 层 (`BSP/`)

| 文件 | 职责 |
|------|------|
| `bsp_usart.c/h` | USART1 流量模组通信（BCD 协议）+ USART2 Modbus RTU 从站（功能码 01/03/04/05/06/10） |
| `key.c/h` | 按键扫描 + 菜单状态机（case 10~60，包含参数设置流程） |
| `eeprom.c/h` | Flash 模拟 EEPROM（Page 63: Span 值, Page 64: DAC 值） |
| `mystring.c/h` | 字符串工具函数（Int2String, insert_char） |

### OLED 层 (`OLED/`)

| 文件 | 职责 |
|------|------|
| `oled.c/h` | SSD1306 驱动，帧缓冲 `OLED_GRAM[144][8]`，SPI bit-bang 底层 |
| `oledfont.h` | 字体数据（6×8, 12×6, 16×8, 24×12 ASCII + 16×16/24×24/32×32/64×64 汉字） |
| `bmp.h` | 位图资源（温度度符号图标） |

### OLED 驱动 API 速查

```c
OLED_Init();                                          // 初始化
OLED_Clear();                                         // 清屏并刷新
OLED_Refresh();                                       // 帧缓冲 → 硬件
OLED_DrawPoint(x, y, t);                             // 画点: t=1填充, t=0清除
OLED_ShowChar(x, y, chr, size1, mode);               // 单字符, size1=8/12/16/24
OLED_ShowString(x, y, *chr, size1, mode);            // 字符串
OLED_ShowNum(x, y, num, len, size1, mode);           // 无符号整数
OLED_ShowChinese(x, y, num, size1, mode);            // 汉字（索引号）
OLED_ShowPicture(x, y, sx, sy, BMP[], mode);         // 位图
OLED_ColorTurn(i);                                    // i=0正常, i=1反色
OLED_DisplayTurn(i);                                  // i=0正常, i=1旋转180°
```

## 数据变量

### 传感器数据 (`bsp_usart.h`)

| 变量 | 类型 | 来源 | 含义 |
|------|------|------|------|
| `FlowRateValue` | `Uart_SendfloatTypeDef` (union) | UART1 BCD | 瞬时流量 |
| `FlowTemperature` | `Uart_SendfloatTypeDef` (union) | UART1 BCD | 温度 |
| `FlowPressure` | `Uart_SendfloatTypeDef` (union) | UART1 BCD | 压力 |
| `Cumulativeflow` | `uint64_t` | 累加计算 | 累积流量 |
| `strFlowSumBuf` | `unsigned char[20]` | BCD 转字符串 | 累积流量字符串 |
| `strFlowRateBuf` | `unsigned char[20]` | BCD 转字符串 | 瞬时流量字符串（整数部分） |
| `strFlowRate_2Buf` | `unsigned char[10]` | BCD 转字符串 | 瞬时流量字符串（小数部分） |
| `strFlowTemBuf` | `unsigned char[20]` | BCD 转字符串 | 温度字符串 |
| `strFlowPressBuf` | `unsigned char[20]` | BCD 转字符串 | 压力字符串 |
| `Sumunit` | `uint8_t` | 参数 | 0=L, 1=m³ |
| `ModuleState` | `uint8_t` | UART 状态 | 0=ok, 非0=Err |

### DAC/校准数据 (`main.h`)

| 变量 | 类型 | 说明 |
|------|------|------|
| `DacValueBuf[2]` | `uint16_t[2]` | DAC 零点/满度值（Flash Page 64） |
| `DacValue` | `uint16_t` | 当前 DAC 输出值 |
| `SpanValueBuf[2]` | `SpanTypeDef[2]` | 流量量程低/高值（Flash Page 63） |
| `BitControlBuf[50]` | `uint16_t[50]` | Modbus 位控制寄存器 |
| `CalEnabledFlag` | `uint8_t` | 校准模式标志 |
| `ForceDacOutFlag` | `uint8_t` | 强制 DAC 输出标志 |

### 时间基准 (`tim.c`)

| 变量 | 类型 | 周期 | 说明 |
|------|------|------|------|
| `DisplayTimeBase` | `uint8_t` | 10ms | 显示刷新计数器（>= 20 时触发 200ms 刷新） |
| `Timer3Uart1TimeBase10ms` | `uint8_t` | 10ms | UART1 通信时序 |
| `Timer3Uart2TimeBase10ms` | `uint8_t` | 10ms | UART2 通信时序 |

## 当前显示布局

```
行1 (y=0):   Tx/Rx  ok  25.1°C              ← 状态栏 (6×8)
行2 (y=16):  RATE         123.4              ← 瞬时流量 (16×8 字体)
行3 (y=32):               5  L/h             ← 瞬时流量小数 + 单位 (16×8)
行4 (y=48):  TOTAL  m³                      ← 累积流量标签 (16×8)
行5 (y=56):       0000000.0                  ← 累积流量值 (6×8)
```

## 菜单状态机 (`BSP/key.c`)

当前菜单为线性状态机（switch-case），仅支持单层导航：

| 状态码 | 功能 | 操作 |
|--------|------|------|
| 0 | 运行模式 | 正常显示 |
| 10 | 密码验证 | 输入 LOC=556 进入 |
| 20 | Flow-L 设置 | 量程低值（0~999.99 L/H） |
| 30 | Flow-H 设置 | 量程高值（0~999.99 L/H） |
| 40 | DA-ZERO 校准 | DAC 零点（0~65535） |
| 50 | DA-FULL 校准 | DAC 满度（0~65535） |
| 60 | 结束确认 | 显示 END，按键退出 |
| 70/80 | 保留 | 未实现 |

**按键行为**：
- K_MOV (PC15): 短按确认/进入，长按 400ms 退出
- K_ADD (PA11): 增加 + 自动加速（10→100→1000→10000）
- K_SUB (PA0): 减少 + 自动加速

## EEPROM 存储映射

| Flash 页 | 地址 | 用途 |
|----------|------|------|
| Page 63 | `0x0800FC00` | Span 值（SpanLo, SpanHi: uint32_t × 2） |
| Page 64 | `0x08010000` | DAC 值（DacZero, DacFull: uint16_t × 2） |

**注意**: STM32F103C8 只有 64KB Flash（Page 0~63）。`ADDR_FLASH_PAGE_64` = `0x08010000` 超出 64KB 范围，实际使用的是 Flash 尾部的 Page 63 作为有效数据存储。写入前确认地址范围。

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

- ISR 中只设置标志位/递增计数器，不执行复杂逻辑
- 主循环中根据标志位处理业务逻辑
- ISR 与主循环共享的变量必须声明为 `volatile`
- 按键驱动只返回事件码，不直接调用菜单/显示函数
- 禁止在 ISR 中调用 `OLED_Refresh()`、`HAL_FLASH_xxx()` 等耗时操作

## 代码规范

- 语言: C (C99 兼容)，所有注释使用中文
- 类型: 使用 `float` 进行浮点运算（Cortex-M3 无 FPU，使用软浮点）
- 状态码: `HAL_StatusTypeDef` (HAL_OK / HAL_ERROR / HAL_BUSY / HAL_TIMEOUT)
- STM32CubeMX 生成的 `Core/` 目录代码使用 `/* USER CODE BEGIN/END */` 保护块 — 仅在保护块内修改

## OLED 重构说明

本项目计划使用 **afiskon/stm32-ssd1306** 库替换现有 OLED 驱动层，并参照 coriolis_drive 项目重构显示内容层。
afiskon 库文件位于 `OLED github/afiskon-stm32-ssd1306/ssd1306/`。

### 重构目标

1. 替换底层驱动为 afiskon 库（保留 bit-bang SPI）
2. 实现与 coriolis_drive 一致的 S01（主界面）/ S02（辅助变量页）运行显示
3. 菜单系统重写（参照 coriolis_drive 的 bsp_menu 架构）

### 资源预算

| 资源 | 总量 | 已用 | 可用 | 新增预估 |
|------|------|------|------|----------|
| Flash | 64KB | ~30KB | ~34KB | ~11.4KB (库+字体+渲染) |
| RAM | 20KB | ~6KB | ~14KB | ~1KB (帧缓冲+状态) |

### afiskon 库适配要点

- 底层重写 3 个函数: `ssd1306_Reset()`, `ssd1306_WriteCommand()`, `ssd1306_WriteData()` 为 bit-bang SPI
- GPIO: CLK=PB0, SDA=PA4, RES=PA5, DC=PA6, CS=PA7
- 字体选择: Font_6x8 (~1.1KB) + Font_7x10 (~1.9KB) + Font_11x18 (~3.4KB)，禁用 Font_16x26
- IAR V8.32 兼容: `#include <_ansi.h>` 和 `_BEGIN_STD_C` / `_END_STD_C` 可能需要适配

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

**根本原因**: 变量在 `.c` 中定义时使用了 `volatile`（或其他类型修饰符），但对应的 `.h` 中 `extern` 声明没有同步修改。编译器要求**定义**和**声明**的类型完全一致。

**修复方法**: `.h` 中的 `extern` 声明必须与 `.c` 中的**定义**完全匹配，包括 `volatile`、`const` 等修饰符。

```c
// tim.h
extern volatile uint8_t DisplayTimeBase;  // 必须与 tim.c 中的定义一致

// tim.c
volatile uint8_t DisplayTimeBase;         // 定义
```

**排查技巧**: 搜索 `Error[Pa165]` 报出的变量名，对比 `.h` 的 `extern` 声明和 `.c` 的定义行。

### 3. Warning[Pe188]: 枚举类型混用 (enumerated type mixed with another type)

**症状**: 将算术运算结果（`int` 类型）赋值给 `enum` 变量。

**根本原因**: C 语言中枚举值在表达式中会被提升为 `int`，算术运算后结果为 `int`，再赋回 `enum` 变量时编译器发出警告。IAR 默认开启此警告。

**修复方法**: 添加显式类型转换 `(enum_type_t)`。

```c
// 警告写法
s_current_page = (s_current_page + 1) % RUN_PAGE_COUNT;

// 正确写法
s_current_page = (run_page_t)((s_current_page + 1) % RUN_PAGE_COUNT);
```

### 4. Warning[Pe223]: 隐式函数声明 (function declared implicitly)

**症状**: 调用了某个函数但编译器不知道其原型，按隐式规则（返回 `int`、参数未知）处理。

**根本原因**: 缺少对应的 `#include` 头文件，或头文件中未声明该函数。

**修复方法**: 在调用处所在 `.c` 文件顶部 `#include` 对应的头文件。

```
Warning[Pe223]: function "ssd1306_UpdateScreen" declared implicitly
→ 在 main.c 中添加 #include "ssd1306.h"
```

### 5. Warning[Pe550]: 变量赋值后未使用 (variable set but never used)

**症状**: 变量被赋值但从未被读取。

**修复方法**:
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

## Git 与文档管理规则

- **项目文档**: `README.md` 为项目主文档，包含功能描述、构建方法、文件结构、版本历史等
- **推送前必须更新 `README.md`**: 每次向 GitHub 推送前，必须先更新 `README.md` 中的版本日志和变更摘要，确保文档与代码同步
- **注释语言**: 所有注释和文档使用中文
