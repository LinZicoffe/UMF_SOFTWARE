# embed-ai-tool 万能使用模板教程

> 适用于任何嵌入式项目的 Claude Code Skill 使用指南。
> 仓库: https://github.com/LeoKemp223/embed-ai-tool.git

## 一、安装方法

### 全量安装（推荐）

```bash
git clone https://github.com/LeoKemp223/embed-ai-tool.git /tmp/embed-ai-tool
cp -r /tmp/embed-ai-tool/skills/*  your-project/.claude/skills/
cp -r /tmp/embed-ai-tool/shared    your-project/.claude/skills/shared
cp -r /tmp/embed-ai-tool/scripts   your-project/.claude/skills/scripts
rm -rf /tmp/embed-ai-tool
```

### 按需安装

只复制项目实际需要的 skill：

```bash
git clone https://github.com/LeoKemp223/embed-ai-tool.git /tmp/embed-ai-tool

# CMake 项目最小集
cp -r /tmp/embed-ai-tool/skills/build-cmake    your-project/.claude/skills/
cp -r /tmp/embed-ai-tool/skills/flash-openocd  your-project/.claude/skills/
cp -r /tmp/embed-ai-tool/skills/serial-monitor your-project/.claude/skills/

# IAR 项目最小集
cp -r /tmp/embed-ai-tool/skills/build-iar      your-project/.claude/skills/

# Keil 项目最小集
cp -r /tmp/embed-ai-tool/skills/build-keil     your-project/.claude/skills/
cp -r /tmp/embed-ai-tool/skills/flash-keil     your-project/.claude/skills/

# 共享资源（必须）
cp -r /tmp/embed-ai-tool/shared                your-project/.claude/skills/shared

rm -rf /tmp/embed-ai-tool
```

### 全部可用 Skill 清单

| Skill | 说明 | 适用工具链 |
|-------|------|-----------|
| `build-cmake` | CMake 工程编译 | CMake + arm-none-eabi-gcc |
| `build-iar` | IAR EWARM 命令行编译 | IAR Embedded Workbench |
| `build-keil` | Keil MDK 编译 | Keil MDK (UV4) |
| `build-platformio` | PlatformIO 编译 | PlatformIO CLI |
| `build-idf` | ESP-IDF 编译 | ESP-IDF 工具链 |
| `flash-keil` | Keil 内置调试器烧录 | Keil MDK |
| `flash-openocd` | OpenOCD 烧录 | OpenOCD + JTAG/SWD |
| `flash-platformio` | PlatformIO 烧录 | PlatformIO CLI |
| `flash-idf` | ESP-IDF 烧录 | esptool / idf.py |
| `debug-gdb-openocd` | GDB + OpenOCD 调试 | arm-none-eabi-gdb |
| `debug-platformio` | PlatformIO 内置 GDB 调试 | PlatformIO CLI |
| `serial-monitor` | 串口日志抓取 | pyserial |
| `modbus-debug` | Modbus RTU/TCP 调试 | pymodbus |
| `can-debug` | CAN 总线调试 | python-can |
| `visa-debug` | VISA 仪器 SCPI 调试 | pyvisa |
| `peripheral-driver` | 外设驱动搜索与适配 | 无外部依赖 |
| `stm32-hal-development` | STM32 HAL 开发指导 | 无外部依赖 |
| `workflow` | 编译+烧录+监控流水线 | 依赖对应构建 skill |

## 二、通用使用模式

Skill 有两种触发方式，效果完全等价：

### 方式 1: 自然语言

在对话中用自然语言描述需求，Claude 自动匹配 skill：

```
"用 IAR 编译一下"
"帮我适配一个 MPU6050 驱动"
"调试 Modbus 从站地址 2 的寄存器"
"看下串口日志"
"编译烧录一条龙"
```

### 方式 2: 斜杠命令

直接输入 skill 名称（不含路径）：

```
/build-iar
/modbus-debug
/serial-monitor
/workflow
```

## 三、按开发阶段使用 Skill

### 阶段 1: 项目初始化

```bash
# 1. 检查 CubeMX 生成的 HAL 工程结构
/stm32-hal-development

# 2. 添加新外设驱动（自动搜索开源库并适配）
# 示例: 添加 I2C EEPROM 驱动
/peripheral-driver
# 然后告诉 Claude: "帮我适配 AT24C02 驱动，I2C 总线，handle 是 hi2c1，地址 0x50"

# 示例: 添加 SPI 显示屏驱动
/peripheral-driver
# 然后告诉 Claude: "帮我适配 SSD1306 驱动，SPI 总线，handle 是 hspi1"
```

### 阶段 2: 编译构建

根据工具链选择对应 skill：

```bash
# IAR 项目
/build-iar
# Claude 会自动找到 .ewp 文件并编译

# CMake 项目
/build-cmake

# Keil 项目
/build-keil

# PlatformIO 项目
/build-platformio

# ESP-IDF 项目
/build-idf
```

### 阶段 3: 烧录

```bash
# OpenOCD 烧录（通用，需硬件调试器如 ST-Link、J-Link）
/flash-openocd

# Keil 内置烧录
/flash-keil

# PlatformIO 烧录
/flash-platformio

# ESP-IDF 烧录
/flash-idf
```

### 阶段 4: 调试验证

```bash
# 串口日志（查看 printf/LOG 输出）
/serial-monitor
# 然后: "抓取 10 秒日志，COM3，115200 波特率"

# Modbus 协议调试（适用于仪表、PLC 通信）
/modbus-debug
# 然后: "读从站地址 2，地址 0 开始 10 个保持寄存器"

# CAN 总线调试
/can-debug

# GDB 在线调试（断点、单步、寄存器查看）
/debug-gdb-openocd

# VISA 仪器调试（示波器、万用表等）
/visa-debug
```

### 阶段 5: 流水线一键执行

```bash
/workflow
# 然后: "编译 → 烧录 → 串口监控，用 IAR 构建"
```

## 四、各 Skill 详细用法速查

### 4.1 build-iar

```bash
# 环境探测（确认 IAR 已安装且 iarbuild.exe 可用）
python .claude/skills/build-iar/scripts/iar_builder.py --detect

# 扫描工作区中的 .ewp/.eww 文件
python .claude/skills/build-iar/scripts/iar_builder.py --scan

# 列出工程中的可用配置（Debug/Release 等）
python .claude/skills/build-iar/scripts/iar_builder.py \
  --list-configs --project path/to/project.ewp

# 编译指定配置
python .claude/skills/build-iar/scripts/iar_builder.py \
  --project path/to/project.ewp --config Debug
```

**输出**: 编译结果、错误/警告统计、固件产物路径（.out/.hex/.bin）

### 4.2 build-cmake

```bash
# 配置并编译
python .claude/skills/build-cmake/scripts/cmake_builder.py \
  --project-dir /path/to/project --build

# 指定工具链文件
python .claude/skills/build-cmake/scripts/cmake_builder.py \
  --project-dir /path/to/project --toolchain arm-none-eabi-gcc.cmake --build
```

### 4.3 build-keil

```bash
# 环境探测
python .claude/skills/build-keil/scripts/keil_builder.py --detect

# 编译
python .claude/skills/build-keil/scripts/keil_builder.py \
  --project path/to/project.uvprojx --build
```

### 4.4 peripheral-driver

```bash
# 扫描已有驱动代码，生成适配建议报告
python .claude/skills/peripheral-driver/scripts/bsp_adapter.py \
  --scan ./downloaded_driver/

# 将开源驱动适配到 BSP 规范
python .claude/skills/peripheral-driver/scripts/bsp_adapter.py \
  --adapt ./downloaded_driver/ \
  --device <设备名> --handle <HAL句柄> \
  --output ./BSP/bsp_<device>/

# 无开源库时生成 BSP 骨架文件
python .claude/skills/peripheral-driver/scripts/bsp_adapter.py \
  --scaffold --device <设备名> --bus <总线类型> \
  --handle <HAL句柄> --addr <I2C地址> \
  --output ./BSP/bsp_<device>/

# 总线类型: i2c / spi / uart / 1wire / gpio
```

### 4.5 modbus-debug

```bash
# 依赖安装
pip install pymodbus pyserial

# 环境探测
python .claude/skills/modbus-debug/scripts/modbus_tool.py --detect

# 读保持寄存器 (FC03)
python .claude/skills/modbus-debug/scripts/modbus_tool.py \
  --port COM3 --slave 1 --read --address 0 --count 10

# 写单个寄存器 (FC06)
python .claude/skills/modbus-debug/scripts/modbus_tool.py \
  --port COM3 --slave 1 --write --address 0 --values 100

# 写多个寄存器 (FC16)
python .claude/skills/modbus-debug/scripts/modbus_tool.py \
  --port COM3 --slave 1 --write --address 0 --values 100,200,300

# 读输入寄存器 (FC04)
python .claude/skills/modbus-debug/scripts/modbus_tool.py \
  --port COM3 --slave 1 --read-input --address 0 --count 5

# 读线圈 (FC01)
python .claude/skills/modbus-debug/scripts/modbus_tool.py \
  --port COM3 --slave 1 --read-coils --address 0 --count 8

# 扫描总线上的从站
python .claude/skills/modbus-debug/scripts/modbus_tool.py \
  --port COM3 --scan --scan-range 1-247

# TCP 模式（替换 --port 为 --tcp --host）
python .claude/skills/modbus-debug/scripts/modbus_tool.py \
  --tcp --host 192.168.1.100 --slave 1 --read --address 0 --count 10
```

### 4.6 serial-monitor

```bash
# 依赖安装
pip install pyserial

# 列出所有可用串口
python .claude/skills/serial-monitor/scripts/serial_monitor.py --list

# 自动选择串口
python .claude/skills/serial-monitor/scripts/serial_monitor.py --auto

# 抓取指定时长日志
python .claude/skills/serial-monitor/scripts/serial_monitor.py \
  --port COM3 --baud 115200 --duration 10

# 持续监视（Ctrl+C 停止）
python .claude/skills/serial-monitor/scripts/serial_monitor.py \
  --port COM3 --baud 115200 --monitor --timestamp

# 等待特定字符串出现
python .claude/skills/serial-monitor/scripts/serial_monitor.py \
  --port COM3 --baud 115200 --wait "System Ready"

# 先开始监听，再复位目标板（需 OpenOCD）
python .claude/skills/serial-monitor/scripts/serial_monitor.py \
  --port COM3 --baud 115200 --wait-reset --auto-reset

# 保存日志到文件
python .claude/skills/serial-monitor/scripts/serial_monitor.py \
  --port COM3 --baud 115200 --monitor --save output.log
```

### 4.7 workflow

```bash
# 探测环境
python .claude/skills/workflow/scripts/workflow_runner.py --detect

# 查看可用流水线
python .claude/skills/workflow/scripts/workflow_runner.py --list

# 执行: 编译 → 烧录 → 串口监控
python .claude/skills/workflow/scripts/workflow_runner.py \
  --run build-flash-monitor \
  --build-system <iar|cmake|keil|platformio> \
  --project /path/to/project

# 执行: 编译 → 烧录 → GDB 调试
python .claude/skills/workflow/scripts/workflow_runner.py \
  --run build-flash-debug \
  --build-system <iar|cmake|keil|platformio> \
  --project /path/to/project
```

### 4.8 debug-gdb-openocd

```bash
# 依赖: arm-none-eabi-gdb, openocd

# 下载固件后开始调试
python .claude/skills/debug-gdb-openocd/scripts/gdb_debug.py \
  --elf path/to/firmware.out --load

# 仅附着到运行中的目标（不下载）
python .claude/skills/debug-gdb-openocd/scripts/gdb_debug.py \
  --elf path/to/firmware.out --attach

# 崩溃现场排查
python .claude/skills/debug-gdb-openocd/scripts/gdb_debug.py \
  --elf path/to/firmware.out --crash
```

## 五、不同项目类型的推荐 Skill 组合

### STM32 + IAR 项目（如 UMF）

```
build-iar + stm32-hal-development + peripheral-driver + modbus-debug + serial-monitor + workflow
```

### STM32 + Keil 项目

```
build-keil + flash-keil + stm32-hal-development + peripheral-driver + serial-monitor + workflow
```

### STM32 + CMake + OpenOCD 项目

```
build-cmake + flash-openocd + debug-gdb-openocd + stm32-hal-development + serial-monitor + workflow
```

### ESP32 + ESP-IDF 项目

```
build-idf + flash-idf + serial-monitor + workflow
```

### PlatformIO 通用项目

```
build-platformio + flash-platformio + debug-platformio + serial-monitor + workflow
```

### 工业 Modbus 设备调试

```
modbus-debug + serial-monitor
```

## 六、自然语言触发示例

以下语句在对话中均可自动匹配对应 skill：

| 你说的 | 触发的 Skill |
|--------|-------------|
| "编译一下" | `build-iar` / `build-cmake` / `build-keil`（自动检测） |
| "帮我适配 MPU6050 驱动" | `peripheral-driver` |
| "STM32 HAL 怎么配置 DMA" | `stm32-hal-development` |
| "看下串口输出" | `serial-monitor` |
| "读一下 Modbus 寄存器" | `modbus-debug` |
| "编译烧录" | `workflow` |
| "调试下 UART" | `debug-gdb-openocd` |
| "扫描 CAN 总线" | `can-debug` |
| "读一下示波器波形" | `visa-debug` |

## 七、工具路径配置

部分 skill 依赖外部工具，可全局配置路径避免每次手动指定：

```bash
# 设置 IAR 路径
python .claude/skills/scripts/em_config.py set iarbuild "C:\Program Files\IAR Systems\Embedded Workbench 8.2\arm\bin\iarbuild.exe"

# 设置 OpenOCD 路径
python .claude/skills/scripts/em_config.py set openocd /usr/bin/openocd

# 设置 Keil UV4 路径
python .claude/skills/scripts/em_config.py set uv4 "C:\Keil_v5\UV4\UV4.exe"

# 查看已配置的工具
python .claude/skills/scripts/em_config.py list

# 全局配置（对所有项目生效，加 --global）
python .claude/skills/scripts/em_config.py set openocd /usr/bin/openocd --global
```

## 八、故障排查

| 问题 | 原因 | 解决 |
|------|------|------|
| `environment-missing` | 工具未安装或路径未配置 | 运行 `--detect` 确认，用 `em_config.py` 配置路径 |
| `ambiguous-context` | 多个工程文件或串口候选 | 明确指定工程路径或串口号 |
| `connection-failure` | 串口打不开或网络不通 | 检查权限、驱动、连线 |
| `target-response-abnormal` | 固件崩溃或启动失败 | 用 `debug-gdb-openocd` 排查 |
| `artifact-missing` | 编译成功但找不到产物 | 检查输出目录配置 |
| Python 脚本报错 | 缺少依赖库 | `pip install pymodbus pyserial pyvisa python-can` |
