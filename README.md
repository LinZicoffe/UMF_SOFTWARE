# UMF 超声波流量传感器固件

UMF (Ultrasonic Meter Firmware) — 基于 STM32F103C8T6 的超声波流量传感器嵌入式固件。

## 功能概述

- **超声波流量采集**: 通过 USART1 与 UFL-1A 超声波流量模组通信（自定义 BCD 协议），解析瞬时流量、温度、压力、累积流量
- **Modbus RTU 从站**: USART2 作为 Modbus RTU 从站（地址 2），支持功能码 01/03/04/05/06/10
- **4~20mA DAC 输出**: TIM1/TIM4 PWM 模拟输出，支持零点和满度校准
- **OLED 显示**: SSD1306 128×64，SPI bit-bang 驱动，支持 S01 主界面和 S02 辅助变量页切换
- **参数存储**: Flash 模拟 EEPROM，Page 55~63 分组存储仪表参数、量程范围和 DAC 校准值
- **菜单系统**: 5 层导航栈 + 6 种界面模式 (列表/数值/枚举/密码/只读/确认) + 两级密码门控 (操作员/工程师)
- **中英文双语菜单**: 16x16 中文字模渲染，菜单系统全面支持中英文切换（S44 Language 设置）
- **全参数配置**: 基本设置、输出设置、介质/工况、累积器、累计总量管理、校准、系统设置共 42 个屏幕

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
│   ├── eeprom.c/h                # Flash 模拟 EEPROM (磨损均衡日志结构, Page 55~63)
│   ├── mystring.c/h              # 字符串工具 (Int2String, insert_char)
│   └── run_display.c/h           # 运行显示模块 (S01 主界面 + S02 辅助页)
├── OLED/                          # OLED 显示驱动
│   ├── ssd1306_conf.h            # afiskon 库硬件配置 (引脚/字体/SPI 模式)
│   ├── ssd1306.c/h               # afiskon SSD1306 驱动 (bit-bang SPI 适配)
│   ├── ssd1306_fonts.c/h         # 字体数据 (6x8, 7x10, 11x18, 16x26)
│   ├── chinese_font.c/h         # 16x16 中文字模数据 + 混合字符串渲染 (WriteMixedStr)
│   ├── chinese_font_data.h      # 中文字模索引定义 (CHI_xxx 宏)
│   ├── generate_chinese_font.py # 中文字模生成脚本 (Hzk16 格式)
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
├── UMF_HostApplication_Development_Plan.md  # 上位机开发计划 (C# WPF)
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
  └── 5.System       → S04 密码 → S39 系统设置 (5 项)
      └── S44 Language (中文/英文切换)
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

| 地址 | 功能 | 数据类型 | 读写 |
|------|------|----------|------|
| 40001 | 瞬时流量 | float | FC03 读 |
| 40003 | 温度 | float | FC03 读 |
| 40005 | 压力 | float | FC03 读 |
| 40021~40022 | DAC 零点/满度 | uint16 | FC03/FC10 |
| 40023~40024 | 流量单位/累积单位 | uint16 | FC03/FC06 |
| 40025~40026 | 仪表系数 | float | FC03/FC06 |
| 40027~40028 | 介质系数 | float | FC03/FC06 |
| 40029~40030 | 小信号切除 | float | FC03/FC06 |
| 40031~40032 | 量程低/高值 | float | FC03/FC10 |
| 40041 | 累积流量 | uint64 | FC03 读 |
| 40049 | 模拟总开关 | uint16 | FC03/FC06 |
| 40051~40052 | 模拟瞬时流量 | float | FC03/FC06 |
| 40053~40054 | 模拟温度 | float | FC03/FC06 |
| 40055~40056 | 模拟累积流量 | float | FC03/FC06 |
| 40061 | 通信状态 ModuleState | uint16 | FC03 读 |
| 40062~40063 | 正向累积 forward_total | float | FC03 读 |
| 40064~40065 | 反向累积 reverse_total | float | FC03 读 |
| 40066~40067 | 净累积 (正-反) | float | FC03 读 |
| 40068~40069 | 实时 4-20mA 电流 | float | FC03 读 |
| 40070 | 标准工况 std_cond | uint16 | FC03/FC06 |
| 40071~40072 | 滤波参数 filter_time | float | FC03/FC06 |
| 40073~40074 | 阻尼时间 damping_time | float | FC03/FC06 |
| 40075~40076 | 频率输出 freq_output | float | FC03/FC06 |
| 40077 | 脉冲当量 pulse_equiv | uint16 | FC03/FC06 |
| 40078~40079 | 介质密度 density | float | FC03/FC06 |
| 40080~40081 | 管径 pipe_diameter | float | FC03/FC06 |
| 40082~40083 | 气参压力 gas_ref_press | float | FC03/FC06 |
| 40084~40085 | 气参温度 gas_ref_temp | float | FC03/FC06 |
| 40086~40087 | 雷诺系数 reynolds_k | float | FC03/FC06 |
| 40088~40089 | 累积系数 total_factor | float | FC03/FC06 |
| 40090~40091 | 预设总量 preset_total | float | FC03/FC06 |
| 40092 | 通信地址 modbus_addr | uint16 | FC03/FC06 |
| 40093 | 波特率 baud_rate | uint16 | FC03/FC06 |
| 40094 | 语言 language | uint16 | FC03/FC06 |

> **模拟参数**: 通过 FC06 写入模拟值并开启总开关 (40049=1) 后，OLED 显示和 DAC 输出将跟随模拟值。关闭总开关 (40049=0) 恢复真实传感器数据。模拟参数掉电不保存。

> **扩展参数**: 寄存器 40061~40094 为扩展参数区。第一批 (40061~40069) 为只读运行数据；第二批 (40070~40091) 为读写配置参数，通过 param_storage 模块持久化；通信地址 (40092) 可通过 FC06/FC10 修改，写入后立即生效并持久化到 Flash（注意：修改后上位机需切换到新地址才能继续通信）；波特率 (40093) 可通过 FC06/FC10 修改，写入后延迟生效（确保响应在旧波特率下发送完成后再切换）并持久化到 Flash（注意：修改后上位机需切换到新波特率才能继续通信）。float 参数占用 2 个连续寄存器，FC06 分次写入时低位字先缓存、高位字到达后触发 setter 提交。

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

### v1.9.2 (2026-04-29)

- **Modbus 响应地址修复**: 所有功能码 (FC01/03/04/05/06/10) 响应帧地址字节从 `s_modbus_addr` 改为 `Uart2RxBuffer[0]`，确保通过 FC06/FC10 修改设备地址后，响应帧地址仍与请求帧一致，上位机能正常接收确认

### v1.9.0 (2026-04-29)

- **Modbus 扩展参数寄存器**: 新增寄存器地址 60~93，共 34 个寄存器，分三批：
  - **第一批 (只读运行数据, 60~68)**: 通信状态 ModuleState (uint16)、正向累积/反向累积/净累积 (float)、实时 4-20mA 电流 (float)
  - **第二批 (读写配置参数, 69~86)**: 标准工况 (uint16)、滤波参数/阻尼时间/频率输出 (float)、脉冲当量 (uint16)、介质密度/管径/气参压力/气参温度/雷诺系数 (float)
  - **第三批 (系统参数, 87~93)**: 累积系数/预设总量 (float, R/W)、通信地址/波特率 (uint16, R)、语言 (uint16, R/W)
- **DAC 电流计算**: 新增 `compute_dac_current_mA()` 辅助函数，实时计算 4~20mA 电流输出值
- **FC03 读处理扩展**: 新增扩展参数区域 (60~93) switch-case 读取逻辑
- **FC06 写单寄存器扩展**: 新增 uint16 枚举参数单次提交 + float 参数分次写入缓冲提交
- **FC10 写多寄存器扩展**: 新增扩展配置参数区域 (69~93) 批量写入支持
- **通信地址远程修改**: FC06/FC10 可写寄存器 40092，写入后立即生效并持久化到 Flash
- **只读保护**: 仅波特率 (40093) 原为只读，现已开放远程修改

### v1.9.1 (2026-04-29)

- **波特率远程修改**: FC06/FC10 可写寄存器 40093，支持通过 Modbus 远程切换波特率
  - 延迟应用机制: 写入后先完成当前响应发送，下一轮 `Uart2_Communication()` 入口时切换硬件波特率
  - `bsp_usart2_apply_baud_rate()`: DeInit → 重设 BaudRate → Init → 重启 DMA + IDLE 中断
  - `bsp_usart2_check_baud_rate_pending()`: 轮询检查待应用标志，确保发送完成后切换
- **菜单波特率即时生效**: OLED 菜单修改波特率后立即调用 `bsp_usart2_apply_baud_rate()`
- **默认波特率修正**: `DEF_BAUD_RATE` 从 3 (38400) 改为 4 (115200)，与 CubeMX 初始化一致
- **启动波特率同步**: `main.c` 初始化时从 param_storage 读取并应用 Flash 保存的波特率

### v1.8.0 (2026-04-28)

- **Modbus 模拟参数功能**: 新增模拟总开关 + 模拟瞬时流量/温度/累积流量，通过 FC06 写入、FC03 读取
  - 寄存器 40049: 模拟总开关 (uint16, 0=OFF/1=ON)
  - 寄存器 40051~40052: 模拟瞬时流量 (float, 2 regs)
  - 寄存器 40053~40054: 模拟温度 (float, 2 regs)
  - 寄存器 40055~40056: 模拟累积流量 (float, 2 regs)
  - 开启总开关后 OLED 显示和 DAC 输出跟随模拟值，关闭后恢复真实传感器数据
  - 模拟参数仅存于 RAM，掉电自动重置为关闭状态
- **FC06 功能码启用**: 重写 `Modbus_Function_6()` 支持模拟参数单寄存器写入
- **OLED SPI 时序微调**: `bitbang_spi_write()` SCL LOW 后插入 NOP 延时
- **新增上位机开发计划**: `UMF_HostApplication_Development_Plan.md` (C# WPF)

### v1.7.2 (2026-04-27)

- **DAC 输出初始化修复**: `DacValue` 初始化从任意值 `8000` 改为 `DacZeroValue`（零点 4mA 对应值），消除上电首次 PWM 输出异常
- **主循环顺序修正**: DAC 线性换算移至 `PWMConfig()` 之前，确保 PWM 输出使用当轮计算的最新值而非滞后一轮
- **TIM1 MOE 安全使能**: `PWMConfig()` 中对 TIM1 高级定时器每次设置 CCR 前显式调用 `__HAL_TIM_MOE_ENABLE()`，防止异常事件导致主输出禁用
- **OLED SPI 时序微调**: `bitbang_spi_write()` SCL LOW 后从 2 个 NOP 减为 1 个 NOP

### v1.7.1 (2026-04-27)

- **OLED SPI 时序优化**: bit-bang SPI 关键位置添加 `__NOP()` 延时，改善数据建立/保持时间裕量
  - `bitbang_spi_write()`: SCL LOW 后插入 2 个 NOP
  - `ssd1306_WriteCommand()`: 字节发送后 CS 拉高前插入 2 个 NOP
  - `ssd1306_WriteData()`: CS LOW 后 DC 切换前插入 2 个 NOP
- **开发工具集成**: 新增 Claude Code 嵌入式开发 Skills (IAR 编译/Modbus 调试/串口监视/外设驱动适配/STM32 HAL 指导/编译烧录流水线)
- **CLAUDE.md 文档更新**: 新增"已安装 Skill 及使用方法"章节
- **新增参考文档**: `embed-ai-tool-guide.md` 嵌入式 AI 工具使用指南

### v1.7.0 (2026-04-25)

- **S01 瞬时流量字体放大**: 从 Font_11x18 (11×18px) 升级为 Font_16x26 (16×26px)
  - 启用 `SSD1306_INCLUDE_FONT_16x26`，Flash 增加约 5KB（剩余 ~14KB）
  - Zone B 垂直居中于状态栏与累积栏之间 (y=19)
  - 保留 Font_11x18 降级路径 (`#elif` 分支)
  - 无需修改 IAR 工程文件，`ssd1306_fonts.c` 已在工程中通过宏控制编译

### v1.6.0 (2026-04-25)

- **中英文双语菜单**: 新增 Language 设置 (S44)，支持中文/英文切换
  - 新增 `chinese_font` 模块: 16x16 中文字模数据 + `WriteMixedStr()` 混合字符串渲染 API
  - 菜单系统全面支持双语: 列表/数值/枚举/密码/只读/确认 6 种模式均有中文渲染函数
  - 中文字符使用 0x80~0xFF 编码索引，ASCII 字符保持原有渲染
  - 系统设置从 4 项扩展至 5 项 (新增 Language 首项)
  - 屏幕总数从 41 增至 42 (新增 S44 Language)
- **小信号切除下限调整**: 最小值从 0.5% 改为 0.0%，允许完全禁用小信号切除
- **Span 默认值保护**: 空 Flash (0xFFFFFFFF) 解析时恢复默认值 (0.0/100.0)，防止首次上电异常
- **IAR 工程文件**: `chinese_font.c` 已添加到 `UMF.ewp` OLED 分组
- **中文字模生成工具**: 新增 `generate_chinese_font.py` 脚本 (Hzk16 格式提取)

### v1.5.0 (2026-04-24)

- **Flash 地址越界修复**: DAC 零点/满度存储从 `ADDR_FLASH_PAGE_64`（0x08010000，超出 64KB 范围）迁移到 `ADDR_FLASH_PAGE_59`（0x0800EC00），消除 HardFault 风险
  - 新增 `DAC_FLASH_PAGE_ADDR` 宏统一管理，涉及 `main.h`、`main.c`、`bsp_menu.c`、`bsp_usart.c` 共 5 处替换
- **Flash 参数持久化补充**: 新增 4 个存储页（Page 55~58），各 setter 自动写 Flash：
  - Page 55：信号处理组 (小信号切除值、滤波时间、阻尼时间)
  - Page 56：输出配置组 (频率输出、脉冲当量、语言)
  - Page 57：介质工况组 (密度、管径、气体参考压力/温度、雷诺系数)
  - Page 58：系统组 (Modbus 地址、波特率、总量系数、预设总量)
- **ReadBufferFlash 哨兵值**: 函数入口预置 `0xFFFFFFFFu` / `0xFFFFu`，Flash 全空时调用方可安全判断
- **ConvertFunc 精度修复**: 去除 `float→int32_t` 截断，改为纯 float 线性插值
- **Modbus FC01 响应修复**: 字节从跳位写入 `TxBuffer[3,5,7…]` 改为连续写入 `TxBuffer[3,4,5,6…]`
- **初始化顺序修复**: `Data_Init()` → `param_storage_init()` 后，Span 值同步方向反转，确保 Flash Page 63 真实值不被硬编码默认值 (0.0/100.0) 覆盖
- **Flash 安全加固**: 写入函数 (`WriteBufferFlash`/`WriteBufferFlash_16`) 补充 `HAL_FLASH_Lock()`；读取函数移除不必要的 `HAL_FLASH_Unlock()`
- **DAC 输出 clamp**: `ConvertFunc()` 返回值 clamp 到 `[DacZeroValue, DacFullValue]`，防止负值导致 uint16_t 异常
- **通信协议防护**:
  - BCD 帧最小长度校验 (<28 字节畸形帧直接丢弃)
  - Modbus RTU 最小帧长度检查 (<8 字节丢弃)
  - FC01 位控制 `(quotient+1)` 越界防护
  - FC04 字节计数先 clamp 再写入 TxBuffer
- **除零保护**: `PWMConfig()` 频率为 0 时直接返回；`lin_clac_x8_y8()` 除零时返回前一个插值点
- **ISR 变量 volatile**: `Timer3InitEnabled`/`Time3InitTimeBase` 添加 `volatile` 修饰
- **数据一致性**:
  - 菜单 Span 修改同步持久化到 Flash Page 63
  - 工厂复位同步清除 DAC/Span Flash 数据
  - `Data_Init` 添加 `DacZeroValue < DacFullValue` 不变式检查
- **Flash 预算优化**: SSD1306 画弧函数 (`DrawArc`/`DrawArcWithRadiusLine`) 及辅助函数用 `#ifdef SSD1306_ENABLE_ARC` 条件编译包裹，默认禁用，节省 4~8KB Flash
- **代码清理**: 删除 7 个未使用全局变量，`render_numeric` 缓冲区扩大到 32 字节，`mystring.h` 头文件保护宏拼写修正

### v1.4.1 (2026-04-24)

- **BUG 修复**: SSD1306 寻址模式不匹配导致显示闪屏/内容移位
  - `ssd1306_Init()` 设置 Horizontal Addressing Mode (0x00)，但 `ssd1306_UpdateScreen()` 使用 Page Addressing Mode 命令 (0xB0)
  - 修复: 改为 Page Addressing Mode (0x02)，与刷新命令一致
  - 影响: 正常方向下侥幸工作，旋转或连续按键后地址偏移累积导致显示异常
- **显示优化**: S01 瞬时流量改为 4 位有效数字自适应格式
  - >=1000: 无小数 (如 1234)，100~999: 1 位小数 (如 123.4)
  - 10~99: 2 位小数 (如 12.34)，0~9: 3 位小数 (如 1.234)

### v1.4.0 (2026-04-23)

- **param_storage → 运行时桥接**: 菜单参数真正接入系统运行
  - Modbus 从站地址运行时可配 (菜单保存后立即生效)
  - 4mA/20mA 量程值由 param_storage RAM 缓存管理, Flash 持久化保持原有 BackupBuf 路径
  - 仪表系数 × 介质系数接入 DAC 流量计算
  - 小信号切除: 流量低于 [量程下限 + 量程×N%] 时 DAC 输出零点
  - S01/S02 显示单位从 param_storage 读取 (通过 INPUT 结构体传入，不直接耦合)
- **OLED 显示优化**: S01 瞬时流量从双行加粗改为单行显示
- **BUG 修复**:
  - FlowClearCmdFlag/FlowRstCmdFlag 命令映射反转 (已有代码 BUG)
  - ISR 共享变量添加 volatile (Timer3Uart1/2TimeBase10ms, Uart1/2HaveData)
  - DAC 校准值菜单修改后持久化到 Flash
  - Flash Page 63 存储格式一致性修正 (Data_Init 与 param_storage 使用同一路径)
  - 密码输入 3 位 (修正原 4 位越界)
  - 密码门控逻辑修正
  - render_list 清屏修复
  - handle_readonly 按键响应修正
- **安全加固**: Modbus FC03/FC04 缓冲区溢出防护 (MbBufferLen 上限检查)
- **代码清理**: 删除旧按键变量 (13 个), ISR 减少无效分支
- **命名规范**: static 变量使用 s_ 前缀 (s_modbus_addr)
- **注释补充**: DMA 背压策略说明

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

LDL

## 许可

版权所有 (c) 2020-2026 liyongtai。保留所有权利。
