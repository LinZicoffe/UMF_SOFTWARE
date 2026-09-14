# UMF 超声波流量传感器固件

UMF (Ultrasonic Meter Firmware) — 基于 STM32F103C8T6 的超声波流量传感器嵌入式固件。

## 功能概述

- **超声波流量采集**: 通过 USART1 与 UFL-1A 超声波流量模组通信（自定义 BCD 协议），解析瞬时流量、温度、压力、累积流量
- **Modbus RTU 从站**: USART2 作为 Modbus RTU 从站（地址 2），支持功能码 01/03/04/05/06/10
- **4~20mA DAC 输出**: TIM1/TIM4 PWM 模拟输出，支持零点和满度校准
- **OLED 显示**: SSD1306 128×64，SPI bit-bang 驱动，支持 S01/S02 页面切换、7×10 字体菜单，以及瞬时/累积流量按所选单位换算显示
- **参数存储**: Flash 模拟 EEPROM，Page 54~63 分组存储仪表参数、量程范围和 DAC 校准值
- **菜单系统**: 5 层导航栈 + 6 种界面模式 (列表/数值/枚举/密码/只读/确认) + 两级密码门控 (操作员/工程师)
- **英文菜单界面**: 纯英文菜单（v1.5.0 V7 起，中文双语支持已移除以释放 Flash；S44 Language 屏幕保留兼容）
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
│   ├── param_storage.c/h         # 参数存储 (RAM 缓存 + Flash 持久化, 含七点标定参数)
│   ├── cal_table.c/h             # 七点流量标定 (分段线性插值, Modbus 95~123)
│   ├── eeprom.c/h                # Flash 模拟 EEPROM (磨损均衡日志结构, Page 54~63)
│   ├── mystring.c/h              # 字符串工具 (Int2String, insert_char, u32_to_str_pad)
│   └── run_display.c/h           # 运行显示模块 (S01 主界面 + S02 辅助页)
├── OLED/                          # OLED 显示驱动
│   ├── ssd1306_conf.h            # afiskon 库硬件配置 (引脚/字体/SPI 模式)
│   ├── ssd1306.c/h               # afiskon SSD1306 驱动 (bit-bang SPI 适配)
│   ├── ssd1306_fonts.c/h         # 字体数据 (6x8, 7x10, 11x18; 16x26 已禁用节省 Flash)
│   ├── generate_chinese_font.py # 中文字模生成脚本 (Hzk16 格式, v1.5.0 V7 后中文已移除)
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
├── CALIBRATION_GUIDE.md           # 七点流量标定教程 (Modbus 指令 + 示例)
├── UMF_HostApplication_Development_Plan.md  # 上位机开发计划 (C# WPF)
└── README.md                      # 本文件
```

## 架构

裸机超级循环（无 RTOS）。主循环 200ms 周期刷新显示，10ms TIM3 中断驱动时间基准。

```
UFL-1A 模组 ──USART1 (DMA+IDLE)──→ BCD 解码 ──→ 流量/温度/压力/累积流量
                                                    │
                              ┌─────────────────────┤
                              ↓                     ↓
                    信号链处理                   USART2 (Modbus)
                    10点滑动窗口去最大值滤波       RTU 从站 站号2
                    ×仪表系数×介质系数                 │
                    ×七点标定修正                      ↓
                              │              SSD1306 OLED 显示
                              ↓              (200ms 刷新)
                    TIM1/TIM4 PWM
                    → 4~20mA DAC
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
      └── S44 Language (仅英文; v1.5.0 V7 后中文已移除)
```

### 密码

| 级别 | 密码 | 说明 |
|------|------|------|
| 操作员 | `000` | 可进入菜单；仪表系数和介质系数限制为 0.800~1.200 |
| 工程师/开发者 | `123` | 可进入所有菜单，两个系数使用完整设备范围 |

> **注意**: 密码输入为 3 位数字 (000~999)。普通用户的系数限制以默认值 1.000 为基准，即 ±20%；工程师/开发者仍受设备安全范围限制。

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
| 40001 | 瞬时流量 (固定 L/h, 见单位约定) | float | FC03 读 |
| 40003 | 温度 (固定 ℃) | float | FC03 读 |
| 40005 | 压力 | float | FC03 读 |
| 40021~40022 | DAC 零点/满度 | uint16 | FC03/FC10 |
| 40023~40024 | 流量单位/累积单位 | uint16 | FC03/FC06 |
| 40025~40026 | 仪表系数 | float | FC03/FC06 |
| 40027~40028 | 介质系数 | float | FC03/FC06 |
| 40029~40030 | 小信号切除 | float | FC03/FC06 |
| 40031~40032 | 量程低/高值 | float | FC03/FC10 |
| 40041 | 累积流量 (BCD 原值, LSB=0.001L/m³, 见单位约定) | uint64 | FC03 读 |
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
| 40095 | OLED 自愈重初始化间隔 oled_recovery_interval | uint16 | FC03/FC06 |
| 40096 | 标定使能 cal_enabled | uint16 | FC03/FC06 |
| 40097~40098 | 标定修正系数 k[0] | float | FC03/FC06 |
| 40099~40100 | 标定修正系数 k[1] | float | FC03/FC06 |
| 40101~40102 | 标定修正系数 k[2] | float | FC03/FC06 |
| 40103~40104 | 标定修正系数 k[3] | float | FC03/FC06 |
| 40105~40106 | 标定修正系数 k[4] | float | FC03/FC06 |
| 40107~40108 | 标定修正系数 k[5] | float | FC03/FC06 |
| 40109~40110 | 标定修正系数 k[6] | float | FC03/FC06 |
| 40111~40112 | 标定点百分比 pct[0] | float | FC03/FC06 |
| 40113~40114 | 标定点百分比 pct[1] | float | FC03/FC06 |
| 40115~40116 | 标定点百分比 pct[2] | float | FC03/FC06 |
| 40117~40118 | 标定点百分比 pct[3] | float | FC03/FC06 |
| 40119~40120 | 标定点百分比 pct[4] | float | FC03/FC06 |
| 40121~40122 | 标定点百分比 pct[5] | float | FC03/FC06 |
| 40123~40124 | 标定点百分比 pct[6] | float | FC03/FC06 |

> **模拟参数**: 通过 FC06 写入模拟值并开启总开关 (40049=1) 后，OLED 显示和 DAC 输出将跟随模拟值。关闭总开关 (40049=0) 恢复真实传感器数据。模拟参数掉电不保存。

> **扩展参数**: 寄存器 40061~40123 为扩展参数区。第一批 (40061~40069) 为只读运行数据；第二批 (40070~40091) 为读写配置参数，通过 param_storage 模块持久化；通信地址 (40092) 可通过 FC06/FC10 修改，写入后立即生效并持久化到 Flash（注意：修改后上位机需切换到新地址才能继续通信）；波特率 (40093) 可通过 FC06/FC10 修改，写入后延迟生效（确保响应在旧波特率下发送完成后再切换）并持久化到 Flash（注意：修改后上位机需切换到新波特率才能继续通信）。float 参数占用 2 个连续寄存器，FC06 分次写入时低位字先缓存、高位字到达后触发 setter 提交。

> **OLED 自愈寄存器** (40095): 单位 100ms，范围 0~600，默认 50 (= 5 秒)。设为 0 时禁用周期性自愈；设为 N 时每 N×100ms 重发一次 SSD1306 完整配置命令，用于从 SPI 瞬态干扰 / 接触不良 / EMI 导致的 OLED 控制器全局状态错乱中恢复（如 segment re-map 翻转、charge pump 失效等）。重初始化不动帧缓冲，下次刷屏周期自动覆盖整屏，用户几乎无感（最多半帧闪烁）。修改后立即生效并持久化到 Flash。

> **七点标定寄存器** (40096~40124): 标定使能 (40096) 为 uint16，0=禁用，1=启用。修正系数 k[0]~k[6] 各为 float (范围 0.5~2.0)，标定点百分比 pct[0]~pct[6] 各为 float (范围 0.0~100.0)。默认 pct 为 [0, 3, 10, 25, 50, 75, 100]。标定启用后，信号链中 flow × meter_coeff × medium_coeff 的结果会经过分段线性插值修正: corrected = flow × k(pct)。所有标定参数持久化到 Flash (Page 61 Len=16 合并组)。

> **单位约定（上位机集成必读）**: Modbus 流量/累积寄存器的工程单位**固定**，与 40023(Flow Unit)/40024(Total Unit) **相互独立**——后两者控制 OLED 的显示单位及数值换算，不影响 Modbus 输出数值。
> - **40001 瞬时流量** (float): 固定 **L/h**。源自 UFL-1A BCD 帧，固件已按帧内 flag 换算（`0x0b`→÷100，`0x1b`→原值），Modbus 直传，**不随 40023 变化**。注意此值未经去极值滤波；OLED 使用经滤波的 `effective_flow_rate()`，并按 40023 换算为 m³/h、L/h、L/min 或 kg/h，因此两者在波动工况下或选择非 L/h 单位时数值可能不同。
> - **40003 温度** (float): 固定 **℃**。
> - **40041 累积流量** (uint64, 4 regs): **直传 UFL-1A BCD 原始计数值，Modbus 路径未做 ÷1000 换算**。其 LSB 由模组每帧 byte[8] flag 决定：`0x0a`→**0.001 L**（默认），`0x1a`→**0.001 m³**。上位机需自行 ÷1000 得到升或 m³，并据 flag 判断单位。OLED 会根据该 flag 和 40024 将数值换算为 m³、L、kg 或 t；质量单位使用介质密度，换算结果四舍五入至 0.001 显示单位。字节序为重排大端 `[40,32,56,48,8,0,24,16]`（见 UMF_Modbus_Protocol.md）。
> - **40062~40067 正/反/净累积** (float): 应用层自维护的 `forward_total`/`reverse_total`，量纲独立于 40041，单位由 40024 标签指示（数值本身未换算）。

## 资源预算

| 资源 | 总量 | 已用 | 剩余 |
|------|------|------|------|
| Flash (代码区) | 54KB (Page 0~53) | ~34KB | ~20KB |
| Flash (EEPROM) | 10KB (Page 54~63) | 参数存储 | — |
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

### v2.3.1 (2026-09-14)

- **瞬时流量滤波升级**
  - 将原先每累计 10 个样本才更新一次的分组滤波，改为 10 点滑动窗口去最大值平均
  - 窗口填满后每收到一个新样本即更新滤波结果，降低输出阶梯感并缩短异常值移出窗口后的恢复时间
  - Modbus 40001 仍直传未经滤波的瞬时流量；OLED 与 DAC 继续使用 `effective_flow_rate()` 的滤波结果，模拟流量路径不受影响
- **菜单权限细化**
  - 操作员密码限制仪表系数和介质系数为 0.800~1.200，工程师/开发者密码使用完整设备安全范围
  - 菜单退出后清除访问级别，下一次进入时重新验证身份
- **密码错误提示修复**
  - 使用系统毫秒时基保证错误提示稳定显示 2 秒，不再依赖主循环调用次数
  - `Password Error!` 提示在 OLED 上水平居中
- **恢复出厂通信配置同步修复**
  - 恢复出厂设置后立即同步 Modbus 从站地址和 USART2 配置，避免 OLED 显示值与实际通信参数在重启前不一致

### v2.3.0 (2026-09-11)

- **OLED 界面字体与布局升级**
  - 主界面状态栏和累积流量区域由 6×8 字体升级为 7×10 字体，通信状态压缩为 `OK`/`ER`
  - 主菜单、二级菜单及第 3/4 级菜单统一采用 7×10 字体和每屏 5 行布局
  - 数值、枚举、只读和确认页面同步调整为 7×10 字体布局；密码页保留 11×18 标题/数字，范围提示改用 7×10
  - 主界面累积流量隐藏最高位以容纳 7×10 字体；底层累积量数据不变
- **OLED 瞬时流量单位换算**
  - 以传感器固定 L/h 数据为输入，根据 Flow Unit 显示 m³/h、L/h、L/min 或 kg/h
  - kg/h 换算使用当前介质密度；Modbus 40001 仍保持固定 L/h
- **OLED 累积流量单位换算**
  - 根据传感器原始单位标志和 Total Unit 显示 m³、L、kg 或 t
  - kg/t 换算使用当前介质密度，所有换算结果四舍五入至 0.001 显示单位
  - Modbus 40041 仍直传原始 BCD 累积计数，不受显示单位影响
- **系数按位输入**
  - S07 仪表系数和 S08 介质系数改为固定 `00.000` 格式的 5 位数字编辑
  - UP/DOWN 修改当前位，ENTER 移至下一位，最后一位确认后保存；BACK 取消且不保存
  - 保存时仍按各参数原有范围钳位，参数存储和 Modbus 行为不变

### v2.2.1 (2026-07-08)

- **文档: 明确 Modbus 流量/累积寄存器单位约定**（纯文档/注释澄清，无代码逻辑变更）
  - README 新增"单位约定"小节：40001 瞬时流量固定 L/h，40003 温度固定 ℃，40041 累积流量直传 BCD 原值（LSB 由模组 flag 决定，固件未 ÷1000）
  - 澄清 40023/40024 单位参数仅作 OLED 显示标签，不影响 Modbus 输出数值
  - 寄存器表 40001/40003/40041 行标注固定单位
  - 代码注释同步（`bsp_usart.h` 的 `FlowRateValue`/`Cumulativeflow` 声明）
  - 背景: 默认 `flow_unit=0`(m³/h) 与 40001 实际量纲(L/h) 存在标签错配，本次澄清以避免上位机集成时单位误用

### v2.2.0 (2026-05-21)

- **Flash 优化: 移除 snprintf/stdio 运行时依赖**
  - `bsp_menu.c` 和 `bsp_usart.c` 全部移除 `#include <stdio.h>` 和 `snprintf` 调用
  - 5 处 `snprintf` 替换为手写字符串操作 (`strcpy`/`strcat`/`Int2String`/`u32_to_str_pad`)
  - 完全移除 `xprintfsmall_nomb.o` (1,265B) 等 printf 运行时库
  - ro code 从 35,462 降至 34,225，净节省 **1,237 字节**
  - 不影响 Modbus 读写和 DAC 输出，仅修改 OLED 显示渲染路径
- **ftoa 轻量 float→string 模块**: 纯整数运算替代 `printf %f`，节省 3~8KB Flash
  - `run_display.c` 和 `bsp_menu.c` 中所有 `%.1f`/`%.2f`/`%.3f`/`%.*f` 格式化均已替换为 `ftoa()`
- **UART 配置扩展**: 波特率 → packed `uart_config` (bit[2:0]=baud, bit[4:3]=parity, bit[5]=stop)
  - 新增 2400 波特率选项
  - Modbus 寄存器 40093 写入完整 uart_config，向后兼容旧固件 baud_rate 值
  - `bsp_usart2_apply_baud_rate()` → `bsp_usart2_apply_uart_config()` 支持校验位/停止位

### v2.1.0 (2026-05-08)

- **瞬时流量异常值过滤**: 新增累加器去最大值滤波，去除 BCD 通信毛刺导致的异常 spike（后于 v2.3.1 升级为滑动窗口）
  - 算法: 收集 10 个连续样本，扣除最大值后取 9 个样本均值 (Accumulator Trimmed Mean, K=1)
  - 实现方式: static 内联于 `bsp_usart.c`，无独立模块文件，RAM 14 字节，Flash ~120 字节
  - 喂入点: `Uart1_Receive_Function()` 两处 BCD 解析后调用 `flow_filter_feed()`
  - 输出点: `effective_flow_rate()` 优先返回滤波值，未就绪时回退原始值
  - 模拟流量路径不受影响
  - 设计文档: `DESIGN_flow_outlier_filter.md`

### v2.0.0 (2026-05-05)

- **七点流量标定**: 新增七点分段线性插值标定功能，目标精度 ±1.5% FS
  - 标定点百分比可由上位机通过 Modbus 读写，默认非均匀布局 [0%, 3%, 10%, 25%, 50%, 75%, 100%]
  - 标定修正系数 k[0..6] (范围 0.5~2.0)，标定使能开关独立控制
- **新增 Modbus 标定寄存器 (95~123)**:
  - 40095: 标定使能 (uint16, R/W)
  - 40096~40109: 标定修正系数 k[0]~k[6] (float, 各 2 寄存器, R/W)
  - 40110~40123: 标定点百分比 pct[0]~pct[6] (float, 各 2 寄存器, R/W)
- **Flash 存储扩展**: Page 61 从 Len=1 扩展为 Len=16 合并组 (meter_coeff + cal_enabled + cal_k[7] + cal_pct[7])
- **向后兼容迁移**: 旧固件升级自动迁移 Page 61 数据，保留原有 meter_coeff 值
- **新增 cal_table 模块**: 分段线性插值算法，集成到 DAC 信号链
- **BCD 流量解析修复**: `0x0b` 帧类型瞬时流量错误使用上一帧残留值，修正为从当前帧 BCD 数据计算 (`BCDTOInt(flowrate) / 100.0f`)

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

### v1.5.1 (2026-04-30)

*V7*（**Flash 紧急瘦身 — 移除中文双语界面**）：
- **背景**: V6 修复 HardFault 时把 ROM 区域限制到 Page 0~53 (54KB)，但当前固件代码 + const 数据已膨胀到 ~58.6KB（链接器报 `Lp011 unable to allocate 0xea3e bytes in 0xd714 region`），缺口 4.8KB
- **解决方案**: 移除中英文双语支持，菜单退化为纯英文（保留 `language` 参数枚举但只剩 `LANG_ENGLISH` 单选项）
- **释放空间**: ~8~10KB Flash
  - `chinese_font.c` 字模数据 (110×32B) + 渲染函数 ≈ 5KB
  - `bsp_menu.c` 中文支持代码 (`is_cn_mode` / `get_cn_title` / `get_cn_list_label` / `draw_cn_title` / 6 个 `render_*_cn` 函数 + 中文字符串字面量) ≈ 3~5KB
- **修改文件**:
  - **删除**: `OLED/chinese_font.c` (29KB 源)、`OLED/chinese_font.h` (6KB)、`OLED/chinese_font_data.h` (25KB) 三个文件
  - `EWARM/UMF.ewp`: 移除 `<file>$PROJ_DIR$\..\OLED\chinese_font.c</file>` 编译条目
  - `BSP/bsp_menu.c`: 移除 `#include "chinese_font.h"`、`is_cn_mode()`、`get_cn_title()`、`get_cn_list_label()`、`draw_cn_title()`、6 个 `render_*_cn()` 函数 (~390 行)，移除 6 个 `if (is_cn_mode()) { render_*_cn(f); return; }` 派发分支
  - `BSP/bsp_menu.c`: `s_language_str[LANG_COUNT]` 简化为 `{ "English" }` 单选项
  - `BSP/param_storage.h`: `language_t` 枚举删除 `LANG_CHINESE = 1`，仅保留 `LANG_ENGLISH = 0`
- **影响**:
  - 菜单界面变为**纯英文**，S44 Language 屏幕仍存在但只能选择 "English"
  - 不影响任何核心功能（流量采集 / Modbus / 显示数值 / DAC 输出 / 按键 / 校准 / 参数持久化）
  - Modbus 寄存器 40094 (LanguageReg) 仍可读写但只接受 `0` (LANG_ENGLISH)，写入其他值被 clamp
- **后续恢复中文方案**（如需）:
  - (a) 升级 MCU 到 STM32F103CBT6 (128KB Flash) — 推荐
  - (b) 大幅精简 `bsp_menu.c` 的菜单常量数据
  - (c) 改用更轻量的 16×16 中文字模 (压缩到 4×4 编码方案)

*V6*（**HardFault 紧急修复 — ICF 链接器配置缺陷 + Flash 存储兼容性回滚**）：
- **修复问题**: 设备运行后出现 `HardFault exception` (`PC = 0x0800E064, LR = 0xFFFFFFF9, CFSR.UNDEFINSTR`)
- **根因 A — ICF 链接器配置缺陷（关键）**:
  - `EWARM/stm32f103xb_flash.icf` 中 `__ICFEDIT_region_ROM_end__ = 0x0800FFFF` 把整个 64KB Flash 都纳入 ROM 区
  - 链接器自由地把代码和 const 数据放到 **Page 55~63（EEPROM 模拟区）**
  - 用户做 Modbus 写参数 → `WriteBufferFlash()` 整页擦除 Page 56/58 等 → **代码被擦除成 0xFFFFFFFF**
  - 主循环或 ISR 走到那块代码 → CPU 读 0xFFFFFFFF 当指令执行 → `UNDEFINSTR HardFault`
  - PC = 0x0800E064 落在 Page 56（PARAM_PAGE_OUTPUT, 0x0800E000~0x0800E3FF），证实代码被擦掉
- **根因 B — Flash 存储槽位长度不兼容（次要）**:
  - V5 中把 `flush_system_group` 从 4 字段扩展到 5 字段
  - `WriteBufferFlash` 是链表式追加存储，槽位大小 `(Len+1)*4`：旧固件 Len=4 → 槽位 20 字节；新固件 Len=5 → 槽位 24 字节
  - 旧设备升级新固件后槽位偏移完全错位，旧数据被错误解读（虽不直接 HardFault，但严重削弱可靠性）
- **修复方案（双管齐下）**:
  1. **ICF 修复**: 限制 ROM 代码区到 Page 0~53（54KB，0x08000000~0x0800D7FF），**严格隔离代码与 EEPROM 模拟区**；Page 54~63（10KB）留给 Flash 模拟 EEPROM；链接器再也不会把代码放到 EEPROM 区
  2. **Flash 兼容回滚**: `flush_system_group` 回退到 4 字段（与旧固件兼容），`oled_recovery_interval` 改用**独立的 Page 54** 存储（`PARAM_PAGE_DISPLAY`），避免链表式存储的"槽位长度不可变更"约束
- **修改文件**:
  - `EWARM/stm32f103xb_flash.icf`: `ROM_end` 从 `0x0800FFFF` 改为 `0x0800D7FF`，增加详细注释说明历史教训
  - `BSP/param_storage.c`: 新增 `PARAM_PAGE_DISPLAY = ADDR_FLASH_PAGE_54`；`flush_system_group` 回 4 字段；新增 `flush_display_group`；`param_storage_init` 拆分为 system + display 两段读取；`param_set_oled_recovery_interval` 改调 `flush_display_group`
- **设计原则强化（添加到模块设计原则）**: **Flash 模拟 EEPROM 存储页必须在 ICF 文件中显式排除在代码区外**；扩展 `WriteBufferFlash` 字段长度时**必须使用新页**而不是修改原页 `Len` 参数

*V5*（**OLED 抗干扰加固 — 三层自愈防护**）：
- **新增功能**: OLED bit-bang SPI 抗干扰自愈机制，针对硬件 SPI 接触不良 / EMI / 瞬态干扰导致的显示错乱
  - **第 1 层 — 周期性软重初始化**: 主循环每 N×100ms (默认 5 秒) 调用 `ssd1306_RecoveryInit()` 重发 SSD1306 全套 27 条配置命令；不做硬件复位、不动帧缓冲、不调 UpdateScreen，下次刷屏自动覆盖整屏；菜单激活时跳过避免打断交互
  - **第 2 层 — 每帧关键命令重发**: `ssd1306_UpdateScreen` 入口加固 5 条关键全局命令 (Display ON / Memory Addressing Mode / Charge Pump Enable)，单帧增加 ~150µs 开销，每 200ms 自动修正 OLED 状态机
  - **第 3 层 — SPI 信号完整性改善**: `bitbang_spi_write` 在 SDA 设置后 + SCL 上升沿前增加 1 个 `__NOP()` 延时，提升 OLED 数据建立时间窗口；SPI 频率从 ~3MHz 降到 ~2.5MHz (依然安全)
- **新增 Modbus 寄存器**: 40095 (OledRecoveryAddr=94) — `oled_recovery_interval`
  - 类型: uint16, 单位: 100ms, 范围: 0~600 (0=禁用, 1~600=0.1~60 秒)
  - 默认值: 50 (= 5 秒)
  - 读写: FC03 / FC06 / FC10
  - 持久化: 是 (Flash **Page 54** `PARAM_PAGE_DISPLAY`，独立成页；注: V6 从原 Page 58 system_group 迁出)
  - 修改后立即生效, 上位机可通过 Modbus 远程调整
- **修改文件**:
  - `OLED/ssd1306.c`/`OLED/ssd1306.h`: 新增 `ssd1306_RecoveryInit`，重构提取 `ssd1306_send_init_commands` 静态函数；`ssd1306_UpdateScreen` 加固关键命令；`bitbang_spi_write` 加 NOP
  - `BSP/param_storage.c`/`BSP/param_storage.h`: `param_basic_t` 新增 `oled_recovery_interval` 字段；新增 `param_get/set_oled_recovery_interval`；新增 `PARAM_PAGE_DISPLAY` + `flush_display_group`；`reset_defaults` 重置默认值
  - `BSP/bsp_usart.c`/`BSP/bsp_usart.h`: 新增宏 `OledRecoveryAddr=94`，更新 `ExtParamEndAddr=94`；FC03/FC06/FC10 添加新寄存器处理
  - `Core/Src/tim.c`/`Core/Inc/tim.h`: 新增 `volatile uint16_t OledRecoveryTimeBase` 计数器，TIM3 ISR 每 10ms 递增
  - `Core/Src/main.c`: 主循环检测 `OledRecoveryTimeBase` 达到阈值后调用 `ssd1306_RecoveryInit` 并清零

*V4*（**100% 确认根因 — 决定性修复**）：
- **修复问题**: OLED 显示 ░░░ 实心白方块（压力/温度值、`Tx Err` 中 ER、累积流量数字、°C 符号、菜单文字均出现实心方块替代字符）— 真正根因
  - **诊断方法**: 通过测试 A/B/C/D/E/F 六阶段隔离测试逐步缩小问题范围
    - A: 隔离 OLED 单次显示 → ✅ 正常 (排除 OLED/SPI/字体)
    - B: 全外设+ISR / 主循环空 → ✅ 正常 (排除 ISR 干扰)
    - C: 全业务逻辑 + 固定字符串 → ✅ 正常 (排除业务逻辑破坏显存)
    - D: 全业务逻辑 + `run_display_render` → ✅ 正常 (排除动态渲染逻辑)
    - **E: D + `menu_process` 调用 → ❌ 立即出现乱码 (锁定 bsp_menu.c 链接进来后产生影响)**
    - **F: E + 禁用 Font_16x26 → ✅ 正常 (确认根因)**
  - **根因**: `Font_16x26` 字体表 (~5KB) 在 `bsp_menu.c` 的全部代码 + 大量 const 数据 (菜单字符串、`c_num_desc[45]` 描述符表 ~15KB) 链接进 Flash 后，被 IAR 链接器推到了 64KB Flash 边界外的不存在地址。CPU 读取该地址返回 `0xFF`，字符被渲染成全白实心方块（与现象 100% 吻合）。
  - **解释为什么测试 D 正常**: 测试 D 不引用 `menu_process`，链接器优化掉了 `bsp_menu.c` 的大部分代码和数据，`Font_16x26` 仍在 64KB 安全区内
  - **解释为什么菜单也乱码**: 菜单本身使用的 `Font_6x8`、`Font_7x10`、`Font_11x18` 中至少一个也被推到 Flash 边界附近，部分字形落到不存在区域
  - **修复**: `OLED/ssd1306_conf.h` 注释掉 `#define SSD1306_INCLUDE_FONT_16x26`，释放 ~5KB Flash；`BSP/run_display.c` 已有 `#elif Font_11x18` fallback，瞬时流量大字自动降级到 11×18 像素显示，无需修改其他代码
  - **修改文件**:
    - `OLED/ssd1306_conf.h` — 注释掉 `SSD1306_INCLUDE_FONT_16x26` 宏
    - `Core/Src/main.c` — 移除所有 `OLED_TEST_MODE_X` 测试代码（保留诊断结论注释）
  - **副作用**: S01 主页瞬时流量数字从 16×26 变小为 11×18 像素，但功能完全正常
  - **后续待办**: 若需恢复 16×26 大字，可考虑：(a) 升级 MCU 到 STM32F103C8B 的 128KB 变体；(b) 精简 `bsp_menu.c` 中的 const 数据；(c) 禁用其他不必要字体 (如 `Font_7x10`，仅在备用场景使用)

*V3*（之前的修复，已证实非根因，但作为防御性措施保留）：
- **修复问题**: OLED 显示大量像素错乱（整屏 White 网格背景 + 字符反相）— 假设的 SPI 时序问题
  - 用户实拍照片确认: 上电立即出现, 重启后仍存在, 位置完全固定, 菜单也乱码
  - 通过 git bisect 定位: `eb6378e` (2026-04-28) 和 `1ca1819` (2026-04-27) 两次 commit 在 bit-bang SPI 中加入了大量 NOP 延时 (60+70 NOP/比特)，把 SPI 频率从 ~3MHz 降到 ~500kHz，单帧刷新时长从 ~3.5ms 增到 ~15ms
  - 假设副作用: 慢速 SPI 期间 USART/TIM3 ISR 触发概率大幅增加，ISR 中 USART2_DE/DMA 等 GPIO 操作通过电源/地线寄生耦合到 SPI 信号线 (PB0=CLK 与 PA1=USART2_DE 在相邻引脚)，导致 SDA/SCL 出现毛刺，OLED 控制器收到错乱数据后 GDDRAM 被填入异常 pattern
  - 修复: 回滚 bit-bang SPI 到原始无 NOP 快速版 (~3MHz)，同时在 `ssd1306_WriteCommand`/`ssd1306_WriteData` 中用 PRIMASK 屏蔽中断保护单次 SPI 传输完整性
  - 修改文件: `OLED/ssd1306.c` — `bitbang_spi_write` 删除 130 NOP，`WriteCommand`/`WriteData` 添加 `__disable_irq()`/`__enable_irq()` 保护
  - 影响: 单次 WriteCommand 屏蔽中断 ~3µs，单次 WriteData (128 字节) 屏蔽中断 ~440µs；UART_RX_LEN=150 缓冲足够覆盖 4800bps 下中断屏蔽期间的数据，无丢帧风险
  - 验证: 烧录后用户反馈 "问题依旧存在"，证实非根因。但 PRIMASK 保护和快速 SPI 仍作为防御性改进保留

*V2*：
- **修复问题**: OLED 累积流量末位数字被单位字符覆盖（确定性布局 Bug，每帧必现）
  - 根因: S01 主页 `flow_sum_buf` (13字符×6px=78px) 从 x=30 到 x=107，而单位串 (`total_unit_str`) 从 x=96 写入，导致最后 2 位小数 (x=96~107) 被 "m3"/"L"/"kg"/"t" 覆盖，用户看到单位字符替代数字
  - S02 辅助页同等问题：原 x=42 起，单位在 x=96，覆盖字符 9-12
  - 修复: S01 流量串改为从 x=24（结束于 x=101），单位从 x=102；S02 流量串保持 x=30，单位改为 x=108
  - 修改文件: `BSP/run_display.c`，两处 Zone C 布局坐标调整

*V1*：
- **修复问题**: OLED 显示 ░░░ 乱码根因修复 — CSTACK 从 1024 字节扩大到 2048 字节
  - 根因: ISR 在 snprintf 软浮点格式化最深调用栈时触发，MSP 越过 CSTACK 下边界踩踏 SSD1306_Buffer，导致特定屏幕区域帧缓冲损坏（状态栏 y=0 区域压力值/温度值/℃符号/"Tx Err"等处随机出现方块乱码）
  - 修改文件: `EWARM/stm32f103xb_flash.icf`，`__ICFEDIT_size_cstack__` 0x400 → 0x800
- **修复问题**: ICF 链接脚本 ROM 区域错误配置为 128KB，修正为实际 64KB
  - 原错误: `__ICFEDIT_region_ROM_end__ = 0x0801FFFF`；修正为 `0x0800FFFF`
  - 修改文件: `EWARM/stm32f103xb_flash.icf`
- **修复问题**: `Uart1RxCounter`/`Uart2RxCounter` 未声明 `volatile`，ISR 写/主循环读存在编译器优化缓存风险
  - 修改文件: `BSP/bsp_usart.c`，两个变量添加 `volatile` 修饰
- **优化改进**: `run_display.c` 压力/温度渲染字符缓冲区从 16 扩大到 24 字节，并对压力/温度值进行钳位（press: -999.9~9999.9，temp: -99.9~999.9），防止异常浮点值导致 "C" 字符定位超出屏幕

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

- 原作者：LDL
- 功能更新：黄鑫俊

> 当前分支从提交 `6fb7ce7b1f651f8910ddd0ecde55c304f0c4551e` 起的代码改动均由黄鑫俊编写。该范围内部分提交因电脑 Git 用户配置错误显示为其他姓名，实际作者为黄鑫俊。

## 许可

版权所有 (c) 2020-2026 liyongtai。保留所有权利。
