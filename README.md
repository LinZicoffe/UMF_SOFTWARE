# UMF 超声波流量传感器固件

UMF (Ultrasonic Meter Firmware) — 基于 STM32F103C8T6 的超声波流量传感器嵌入式固件。

## 功能概述

- **超声波流量采集**: 通过 USART1 与 UFL-1A 超声波流量模组通信（自定义 BCD 协议），解析瞬时流量、温度、压力、累积流量
- **Modbus RTU 从站**: USART2 作为 Modbus RTU 从站（地址 2），支持功能码 01/03/04/05/06/10
- **4~20mA DAC 输出**: TIM1/TIM4 PWM 模拟输出，支持零点和满度校准
- **OLED 显示**: SSD1306 128×64，SPI bit-bang 驱动，支持 S01 主界面和 S02 辅助变量页切换
- **参数存储**: Flash 模拟 EEPROM，Page 60~63 存储仪表参数、量程范围和 DAC 校准值
- **菜单系统**: 5 层导航栈 + 6 种界面模式 (列表/数值/枚举/密码/只读/确认) + 两级密码门控 (操作员/工程师)
- **全参数配置**: 基本设置、输出设置、介质/工况、累积器、累计总量管理、校准、系统设置共 41 个屏幕

## 硬件平台

| 项目 | 规格 |
|------|------|
| MCU | STM32F103C8T6 (ARM Cortex-M3, 72MHz, **64KB Flash, 20KB RAM**) |
| OLED | SSD1306 128×64, SPI bit-bang (PB0=CLK, PA4=SDA, PA5=RES, PA6=DC, PA7=CS) |
| 流量模组 | UFL-1A (USART1, PA9/PA10, 自定义 BCD 协议) |
| Modbus | RS-485 (USART2, PA2/PA3, PA1=DE) |
| DAC 输出 | PWM (TIM1_CH1=PA8 高字节, TIM4_CH1=PB6 低字节) |
| 按键 | K_MOV=PC15(向下), K_ADD=PA11(向上), K_SUB=PA0(确认) |
| 指示灯 | PB5 (电源 LED) |

## 构建方法

### 工具链

- **IDE**: IAR Embedded Workbench for ARM (EWARM V8.32)
- **工程文件**: `EWARM/UMF.ewp`
- **工作空间**: `EWARM/Project.eww`
- **启动文件**: `EWARM/startup_stm32f103xb.s`

### 编译步骤

1. 使用 IAR EWARM 打开 `EWARM/Project.eww`
2. 选择 Release 或 Debug 配置
3. Project → Make (F7)
4. 下载程序到目标板

### 注意事项

- 无 Makefile/CMakeLists.txt，仅通过 IAR IDE 构建
- **新增 `.c` 文件必须手动添加到 `EWARM/UMF.ewp`** 中对应 `<group>` 节点
- 编译器宏定义: `USE_HAL_DRIVER`, `STM32F103xB`

## 文件结构

```
UMF_SOFTWARE/
├── Core/                          # STM32CubeMX 生成代码
│   ├── Inc/                       # 头文件 (main.h, tim.h, usart.h, ...)
│   │   ├── main.h                 # 全局类型/变量声明 (DAC, Span, 按键, 位控制)
│   │   └── tim.h                  # 定时器配置与时间基准变量
│   └── Src/                       # 源文件
│       ├── main.c                 # 主循环 + 线性插值 + 数据初始化
│       ├── tim.c                  # TIM1/TIM3/TIM4 配置 + 10ms 中断 + PWM 配置
│       └── usart.c                # UART HAL 配置
├── BSP/                           # 板级支持包
│   ├── bsp_usart.c/h             # USART1 BCD 协议 + USART2 Modbus RTU 从站
│   ├── key.c/h                   # 事件驱动按键驱动 (消抖 + 组合键检测)
│   ├── bsp_menu.c/h              # 菜单系统 (5层导航栈 + 6种模式 + 密码门控)
│   ├── param_storage.c/h         # 参数存储 (RAM 缓存 + Flash 持久化)
│   ├── eeprom.c/h                # Flash 模拟 EEPROM (Page 63/64)
│   ├── mystring.c/h              # 字符串工具 (Int2String, insert_char)
│   └── run_display.c/h           # 运行显示模块 (S01 主界面 + S02 辅助页)
├── OLED/                          # OLED 显示驱动
│   ├── ssd1306_conf.h            # afiskon 库硬件配置 (引脚/字体/SPI 模式)
│   ├── ssd1306.c/h               # afiskon SSD1306 驱动 (bit-bang SPI 适配)
│   ├── ssd1306_fonts.c/h         # 字体数据 (6x8, 7x10, 11x18)
│   ├── oled.c/h                  # 旧版 OLED 驱动 (保留)
│   ├── oledfont.h                # 旧版字体数据
│   └── bmp.h                     # 位图资源 (度符号)
├── Drivers/                       # STM32 HAL + CMSIS 库
├── EWARM/                         # IAR 工程文件
│   ├── UMF.ewp                   # 工程配置
│   ├── Project.eww               # 工作空间
│   └── startup_stm32f103xb.s     # 启动文件
├── UMF_HMI_Screen_Design.md      # UMF HMI 界面设计规格书 (S03~S43)
├── CMF_HMI_Screen_Design_REF.md  # CMF 科里奥利 HMI 参考设计文档
├── CLAUDE.md                      # AI 开发辅助文档
└── README.md                      # 本文件
```

## 架构

裸机超级循环（无 RTOS）。主循环 200ms 周期刷新显示，10ms TIM3 中断驱动时间基准。

```
UFL-1A 模组 ──USART1 (DMA+IDLE)──→ BCD 解码 ──→ 流量/温度/压力/累积流量
                                                    │
                                        ┌───────────┤
                                        ↓           ↓
                              USART2 (Modbus)    TIM1/TIM4 PWM
                              RTU 从站 站号2     → 4~20mA DAC
                                        │
                                        ↓
                              SSD1306 OLED 显示 (200ms 刷新)
```

### 运行显示页面

| 页面 | 内容 | 切换方式 |
|------|------|----------|
| **S01 主界面** | 压力/温度/通信状态(状态栏) + 瞬时流量(大字) + 累积流量 | K_DOWN/K_UP |
| **S02 辅助页** | 流量/流速/温度/压力/DAC电流/频率/通信状态/累积量 | K_DOWN/K_UP |

### 菜单系统

导航栈架构，6 种界面模式，两级密码门控：

```
S03 主菜单 (5 项)
  ├── 1.Display      → 返回运行显示
  ├── 2.Parameter    → S04 密码 → S05 基本设置 (11 项)
  │   ├── S06~S13    基本参数 (标况/仪表系数/介质系数/流量单位/累积单位/小信号/滤波/阻尼)
  │   ├── S14~S18    输出设置 (4mA/20mA/频率/脉冲当量)
  │   ├── S19~S24    介质/工况 (密度/管径/气压/气温/雷诺数)
  │   └── S25~S27    累积器设置 (累积单位/累积系数/预置值)
  ├── 3.Totalizer    → S04 密码 → S28 累计总量管理 (5 项)
  ├── 4.Calibration  → S04 密码 → S34 校准 (4 项)
  └── 5.System       → S04 密码 → S39 系统设置 (4 项)
```

### 密码

| 级别 | 密码 | 说明 |
|------|------|------|
| 操作员 | `000` | 可进入 Parameter 菜单 |
| 工程师 | `123` | 可进入所有菜单 (Parameter/Totalizer/Calibration/System) |

> **注意**: 密码输入为 3 位数字 (000~999)。`pwd_engineer` 默认值已改为 123，可通过 OLED 正常输入。

| 模式 | 用途 | 交互 |
|------|------|------|
| M1 列表 (LIST) | 菜单导航 | K_UP/K_DOWN 移动, K_ENTER 进入, K1+K2 返回 |
| M2 数值 (NUMERIC) | 参数编辑 | K_UP +step, K_DOWN -step, K_ENTER 保存, K1+K2 取消 |
| M3 枚举 (ENUM) | 选项切换 | K_UP/K_DOWN 切换, K_ENTER 确认 |
| M4 只读 (READONLY) | 数据查看 | 任意键返回 |
| M5 确认 (CONFIRM) | 危险操作 | K_UP/K_DOWN YES/NO, K_ENTER 执行 |
| M6 密码 (PASSWORD) | 身份验证 | K_UP/K_DOWN 改数字, K_ENTER 下一位 |

### 按键映射

| 按键 | 引脚 | 菜单功能 | 编辑功能 |
|------|------|---------|---------|
| K1 (K_MOV) | PC15 | 向下选择 | 数值 -step |
| K2 (K_SUB) | PA0  | 确认/进入 | 确认/下一位 |
| K3 (K_ADD) | PA11 | 向上选择 | 数值 +step |
| K1+K2 | — | 返回上一级 | 取消退出 |
| K1+K2+K3 | — | 返回主界面 | 返回主界面 |

> **注意**: 面板实际接线与 CubeMX 引脚命名不同 — PA0(K_SUB) 实为确认键, PC15(K_MOV) 实为向下键。

### Modbus 寄存器映射

| 地址 | 功能 | 数据类型 |
|------|------|----------|
| 40001 | 瞬时流量 | float |
| 40003 | 温度 | float |
| 40005 | 压力 | float |
| 40041 | 累积流量 | uint64 |
| 40021~40022 | DAC 零点/满度 | uint16 |
| 40031~40032 | 量程低/高值 | float |

## 资源预算

| 资源 | 总量 | 已用 | 剩余 |
|------|------|------|------|
| Flash | 64KB | ~45KB | ~19KB |
| RAM | 20KB | ~7KB | ~13KB |

## 调试指南

### 串口调试
- **USART1** (PA9/PA10) — 流量模组通信（BCD 协议）
- **USART2** (PA2/PA3) — Modbus RTU 通信

### 常见问题
1. **系统不启动** — 检查时钟配置和晶振连接
2. **显示异常** — 检查 OLED 接线和 SPI bit-bang 引脚
3. **按键无响应** — 验证 GPIO 配置（注意面板接线与 CubeMX 命名相反）
4. **通信失败** — 检查串口配置和 DMA 设置
5. **DAC 输出异常** — 校准 DA-ZERO 和 DA-FULL

## 版本日志

### v1.4.0 (2026-04-23)

- **param_storage → 运行时桥接**: 菜单参数真正接入系统运行
  - Modbus 从站地址运行时可配 (菜单保存后立即生效)
  - 4mA/20mA 量程值统一由 param_storage 管理 (Flash Page 63)，SpanValueBuf 退化为 Modbus 读缓存
  - 仪表系数 × 介质系数接入 DAC 流量计算
  - 小信号切除: 流量低于 [量程下限 + 量程×N%] 时 DAC 输出零点
  - S01/S02 显示单位从 param_storage 读取 (通过 INPUT 结构体传入，不直接耦合)
- **OLED 显示优化**: S01 瞬时流量从双行加粗改为单行显示
- **菜单系统 BUG 修复**:
  - 密码输入 3 位 (修正原 4 位越界)
  - 密码门控逻辑修正
  - render_list 清屏修复
  - handle_readonly 按键响应修正

### v1.3.0 (2026-04-23)

- **菜单系统重写**: 5 层导航栈架构替换原线性状态机
  - 新增 S03 主菜单 (5 项: 显示/参数/总量/校准/系统)
  - 新增 S04 密码输入 (3 位数字, 操作员/工程师两级门控)
  - 实现 6 种界面模式: 列表/数值编辑/枚举选择/密码/只读/确认
  - 覆盖 S03~S43 共 41 个屏幕 (基本设置/输出/介质/累积器/总量/校准/系统)
- **参数存储扩展**: `param_basic_t` 从 8 字段扩展至 25 字段
  - 新增: 输出设置(4项), 介质/工况(5项), 累积器(2项), 累计总量(2项), 系统(2项), 密码(2项)
  - 新增枚举: `pulse_equiv_t`, `baud_rate_t`
  - Flash Page 60~62 存储策略保持向后兼容
- **按键驱动重写**: 事件驱动模式 (消抖 + 组合键 + 释放触发)
  - 修正面板接线映射: PC15=向下, PA0=确认, PA11=向上
- **主循环重构**: `menu_process()` 统一调度菜单/运行显示按键分发
- **设计文档**: 新增 `UMF_HMI_Screen_Design.md` (界面规格书), `CMF_HMI_Screen_Design_REF.md` (参考文档)

### v1.2.0 (2026-04-23)

- **OLED 显示重构**: 引入 afiskon/stm32-ssd1306 库替换旧驱动
  - bit-bang SPI 底层实现（修复 CS 引脚管理）
  - 新增 S01 主界面（状态栏 + 瞬时流量大字 + 累积流量）
  - 新增 S02 辅助变量页（8 行参数监控）
  - 运行模式 K_DOWN/K_UP 按键翻页
- **ISR 安全**: `DisplayTimeBase` 添加 `volatile` 修饰
- **模块化**: 新增 `run_display` 模块，遵循模块设计原则
- **编译修复**: bmp.h `static const` 修复重复定义，tim.h 声明同步 `volatile`

### v1.1.0 (2024-11-14)

- 菜单状态机完善（case 10~60）
- DAC 校准和 Flash 存储功能
- Modbus RTU 从站功能码支持

### v1.0.0 (2024-10-25)

- 初始版本
- 基本流量采集和 OLED 显示

## 作者

liyongtai (nylyt)

## 许可

版权所有 (c) 2020-2026 liyongtai。保留所有权利。
