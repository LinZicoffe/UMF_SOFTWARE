# UMF 超声波流量传感器固件

UMF (Ultrasonic Meter Firmware) — 基于 STM32F103C8T6 的超声波流量传感器嵌入式固件。

## 功能概述

- **超声波流量采集**: 通过 USART1 与 UFL-1A 超声波流量模组通信（自定义 BCD 协议），解析瞬时流量、温度、压力、累积流量
- **Modbus RTU 从站**: USART2 作为 Modbus RTU 从站（地址 2），支持功能码 01/03/04/05/06/10
- **4~20mA DAC 输出**: TIM1/TIM4 PWM 模拟输出，支持零点和满度校准
- **OLED 显示**: SSD1306 128×64，SPI bit-bang 驱动，支持 S01 主界面和 S02 辅助变量页切换
- **参数存储**: Flash 模拟 EEPROM，存储量程范围和 DAC 校准值
- **按键菜单**: 密码保护（556）进入参数设置，支持 Flow-L / Flow-H / DA-ZERO / DA-FULL

## 硬件平台

| 项目 | 规格 |
|------|------|
| MCU | STM32F103C8T6 (ARM Cortex-M3, 72MHz, **64KB Flash, 20KB RAM**) |
| OLED | SSD1306 128×64, SPI bit-bang (PB0=CLK, PA4=SDA, PA5=RES, PA6=DC, PA7=CS) |
| 流量模组 | UFL-1A (USART1, PA9/PA10, 自定义 BCD 协议) |
| Modbus | RS-485 (USART2, PA2/PA3, PA1=DE) |
| DAC 输出 | PWM (TIM1_CH1=PA8 高字节, TIM4_CH1=PB6 低字节) |
| 按键 | K_MOV=PC15(确认), K_ADD=PA11(增加), K_SUB=PA0(减少) |
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
│   ├── key.c/h                   # 按键扫描 + 菜单状态机 (case 0~60)
│   ├── eeprom.c/h                # Flash 模拟 EEPROM (Page 63/64)
│   ├── mystring.c/h              # 字符串工具 (Int2String, insert_char)
│   └── run_display.c/h           # 运行显示模块 (S01 主界面 + S02 辅助页)
├── OLED/                          # OLED 显示驱动
│   ├── ssd1306_conf.h            # afiskon 库硬件配置 (引脚/字体/SPI 模式)
│   ├── ssd1306.c/h               # afiskon SSD1306 驱动 (bit-bang SPI 适配)
│   ├── ssd1306_fonts.c/h         # 字体数据 (6x8, 7x10, 11x18)
│   ├── oled.c/h                  # 旧版 OLED 驱动 (保留，菜单模式仍使用)
│   ├── oledfont.h                # 旧版字体数据
│   └── bmp.h                     # 位图资源 (度符号)
├── Drivers/                       # STM32 HAL + CMSIS 库
├── EWARM/                         # IAR 工程文件
│   ├── UMF.ewp                   # 工程配置
│   ├── Project.eww               # 工作空间
│   └── startup_stm32f103xb.s     # 启动文件
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
| **S01 主界面** | 压力/温度/通信状态(状态栏) + 瞬时流量(大字) + 累积流量 | K_ADD/K_SUB |
| **S02 辅助页** | 流量/流速/温度/压力/DAC电流/频率/通信状态/累积量 | K_ADD/K_SUB |

### 菜单系统

密码 556 进入，线性状态机: Flow-L → Flow-H → DA-ZERO → DA-FULL → END

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
| Flash | 64KB | ~41KB | ~23KB |
| RAM | 20KB | ~7KB | ~13KB |

## 调试指南

### 串口调试
- **USART1** (PA9/PA10) — 流量模组通信（BCD 协议）
- **USART2** (PA2/PA3) — Modbus RTU 通信

### 常见问题
1. **系统不启动** — 检查时钟配置和晶振连接
2. **显示异常** — 检查 OLED 接线和 SPI bit-bang 引脚
3. **按键无响应** — 验证 GPIO 配置（注意 key.h 宏名与引脚交叉映射）
4. **通信失败** — 检查串口配置和 DMA 设置
5. **DAC 输出异常** — 校准 DA-ZERO 和 DA-FULL

## 版本日志

### v1.2.0 (2026-04-23)

- **OLED 显示重构**: 引入 afiskon/stm32-ssd1306 库替换旧驱动
  - bit-bang SPI 底层实现（修复 CS 引脚管理）
  - 新增 S01 主界面（状态栏 + 瞬时流量大字 + 累积流量）
  - 新增 S02 辅助变量页（8 行参数监控）
  - 运行模式 K_ADD/K_SUB 按键翻页
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

版权所有 (c) 2020-2024 liyongtai。保留所有权利。
