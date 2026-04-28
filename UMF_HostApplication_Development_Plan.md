# UMF 超声波流量传感器上位机开发计划

> **项目代号**: UMF-HostApp
> **版本**: v1.0
> **日期**: 2026-04-28
> **目标平台**: Windows 10/11
> **技术栈**: C# / .NET 8 / WPF / NModbus4

---

## 1. 项目概述

### 1.1 项目目标

为 UMF 超声波流量传感器（STM32F103C8T6 固件）开发一款 Windows 桌面上位机软件，通过 RS-485/USB-RS485 转换器以 **Modbus RTU** 协议与传感器通信，实现：

- 实时流量/温度/压力数据监控与趋势图表
- 累积流量查看与管理
- 4~20mA DAC 输出校准
- 量程参数配置
- 传感器运行控制（校准模式/强制输出/清零/复位）
- 历史数据记录与导出

### 1.2 通信参数

| 参数 | 值 |
|---|---|
| 协议 | Modbus RTU (RS-485) |
| 默认从站地址 | 2 (可配 1~247) |
| 波特率 | 9600 (支持 4800/9600/19200/38400/115200) |
| 数据位 | 8 |
| 校验 | None |
| 停止位 | 1 |
| 超时 | 1000ms |

### 1.3 Modbus 寄存器映射速查

#### FC03 保持寄存器（读取）

| 寄存器地址 | Modbus 地址 | 类型 | 含义 |
|---|---|---|---|
| 0~1 | 40001 | float (IEEE 754) | 瞬时流量 |
| 2~3 | 40003 | float | 介质温度 |
| 4~5 | 40005 | float | 介质压力 |
| 20 | 40021 | uint16 | DAC 零点值 (4mA PWM) |
| 21 | 40022 | uint16 | DAC 满度值 (20mA PWM) |
| 30~31 | 40031 | float | 4mA 量程下限 |
| 32~33 | 40033 | float | 20mA 量程上限 |
| 40~47 | 40041 | uint64 (特殊字节序) | 累积流量 |
| 48 | 40049 | uint16 | **模拟总开关** (0=OFF, 1=ON) |
| 50~51 | 40051 | float | **模拟瞬时流量** |
| 52~53 | 40053 | float | **模拟温度** |
| 54~55 | 40055 | float | **模拟累积流量** |

> **模拟参数说明**：通过 FC06 写入模拟值后，开启总开关 (寄存器 48=1)，OLED 显示和 DAC 输出将跟随模拟值。关闭总开关 (寄存器 48=0) 恢复真实传感器数据。模拟参数掉电不保存。

#### FC01 线圈（读/写）

| 线圈地址 | Modbus 地址 | 含义 |
|---|---|---|
| 0 | 00001 | DAC 校准使能 |
| 1 | 00002 | 强制输出 4mA (零点) |
| 2 | 00003 | 强制输出 20mA (满度) |
| 3 | 00004 | 累积量清零 |
| 4 | 00005 | 流量模组复位 |
| 5 | 00006 | 被动/主动读取模式 |

#### FC05/FC06/FC10 写入控制

| 功能码 | 地址 | 操作 | 前置条件 |
|---|---|---|---|
| FC05 | 00001 | ON: 进入校准模式 | 无 |
| FC05 | 00002 | ON: 强制 4mA 输出 | 校准模式使能 |
| FC05 | 00003 | ON: 强制 20mA 输出 | 校准模式使能 |
| FC05 | 00004 | ON: 累积清零 | 无 |
| FC05 | 00005 | ON: 模组复位 | 无 |
| FC06 | 40049 | 写模拟总开关 (0/1) | 无 |
| FC06 | 40051~40052 | 写模拟瞬时流量 (float, 2次写入) | 无 |
| FC06 | 40053~40054 | 写模拟温度 (float, 2次写入) | 无 |
| FC06 | 40055~40056 | 写模拟累积流量 (float, 2次写入) | 无 |
| FC10 | 40021~40022 | 写 DAC 零点/满度 | 校准模式使能 |
| FC10 | 40031~40033 | 写量程范围 | 校准模式使能 |

---

## 2. 技术架构

### 2.1 技术选型

| 类别 | 选择 | 理由 |
|---|---|---|
| 框架 | **.NET 8** (LTS) | 长期支持、性能优秀、跨平台潜力 |
| UI 框架 | **WPF** | 成熟的数据绑定、MVVM 支持、丰富控件 |
| MVVM 库 | **CommunityToolkit.Mvvm** | 微软官方维护、轻量、Source Generator |
| Modbus 库 | **NModbus4** 或 **FluentModbus** | 成熟稳定、支持 RTU/TCP |
| 串口 | **System.IO.Ports** (.NET 8 内置) | 无需第三方依赖 |
| 图表 | **LiveCharts2** 或 **ScottPlot** | 实时刷新性能好、WPF 支持 |
| 依赖注入 | **Microsoft.Extensions.DependencyInjection** | 标准IoC容器 |
| 日志 | **Serilog** + 文件输出 | 结构化日志、易于排查通信问题 |
| 数据库 | **SQLite** (Microsoft.Data.Sqlite) | 本地轻量存储历史数据，无需安装 |
| 导出 | **ClosedXML** | Excel (.xlsx) 导出，无 Office 依赖 |

### 2.2 项目结构

```
UMF.HostApp/
├── UMF.HostApp.sln
│
├── src/
│   ├── UMF.HostApp/                     # WPF 主程序
│   │   ├── App.xaml / App.xaml.cs
│   │   ├── MainWindow.xaml              # 主窗口 (选项卡导航)
│   │   │
│   │   ├── Views/                       # 视图
│   │   │   ├── DashboardView.xaml       # 实时监控仪表盘
│   │   │   ├── TrendView.xaml           # 趋势图表页
│   │   │   ├── CalibrationView.xaml     # DAC 校准页
│   │   │   ├── ConfigView.xaml          # 参数配置页
│   │   │   ├── ControlView.xaml         # 运行控制页
│   │   │   ├── SimulationView.xaml      # 模拟控制页
│   │   │   ├── HistoryView.xaml         # 历史数据查询页
│   │   │   └── ConnectionView.xaml      # 连接设置页
│   │   │
│   │   ├── ViewModels/                  # 视图模型
│   │   │   ├── DashboardViewModel.cs
│   │   │   ├── TrendViewModel.cs
│   │   │   ├── CalibrationViewModel.cs
│   │   │   ├── ConfigViewModel.cs
│   │   │   ├── ControlViewModel.cs
│   │   │   ├── SimulationViewModel.cs
│   │   │   ├── HistoryViewModel.cs
│   │   │   └── ConnectionViewModel.cs
│   │   │
│   │   ├── Controls/                    # 自定义控件
│   │   │   ├── GaugeControl.xaml        # 圆弧仪表盘控件
│   │   │   ├── NumericIndicator.xaml    # 数值指示器
│   │   │   └── StatusLed.xaml           # 状态指示灯
│   │   │
│   │   ├── Converters/                  # 值转换器
│   │   │   ├── BoolToVisibilityConverter.cs
│   │   │   ├── FloatToStringConverter.cs
│   │   │   └── ConnectionStateToBrushConverter.cs
│   │   │
│   │   ├── Resources/                   # 资源
│   │   │   ├── Styles.xaml              # 全局样式
│   │   │   ├── Icons.xaml               # 图标资源
│   │   │   └── Strings.zh-CN.xaml       # 中文字符串
│   │   │
│   │   └── AssemblyInfo.cs
│   │
│   └── UMF.Core/                        # 核心业务库 (独立于 UI)
│       ├── Communication/               # 通信层
│       │   ├── IModbusService.cs         # Modbus 服务接口
│       │   ├── ModbusRtuService.cs       # Modbus RTU 实现
│       │   ├── ModbusSimulator.cs        # 模拟器 (开发测试用)
│       │   └── SerialPortManager.cs      # 串口管理
│       │
│       ├── Models/                       # 数据模型
│       │   ├── SensorData.cs             # 传感器实时数据
│       │   ├── DacCalibration.cs         # DAC 校准数据
│       │   ├── SpanConfig.cs             # 量程配置
│       │   ├── CoilState.cs              # 线圈状态
│       │   ├── ConnectionConfig.cs       # 连接配置
│       │   └── HistoryRecord.cs          # 历史记录
│       │
│       ├── Services/                     # 业务服务
│       │   ├── IDataService.cs           # 数据采集服务接口
│       │   ├── DataService.cs            # 定时轮询采集
│       │   ├── IHistoryService.cs        # 历史数据服务接口
│       │   ├── HistoryService.cs         # SQLite 存储
│       │   ├── IExportService.cs         # 数据导出接口
│       │   └── ExcelExportService.cs     # Excel 导出
│       │
│       ├── ViewModels/                   # 共享 ViewModel 基类
│       │   └── ViewModelBase.cs
│       │
│       └── Helpers/                      # 工具类
│           ├── ModbusRegisterConverter.cs # 寄存器↔float/uint64 转换
│           ├── DataFormatter.cs           # 数值格式化
│           └── ExceptionHelper.cs         # 异常处理
│
├── tests/
│   └── UMF.Core.Tests/                  # 单元测试
│       ├── ModbusRegisterConverterTests.cs
│       ├── DataServiceTests.cs
│       └── HistoryServiceTests.cs
│
├── docs/                                # 文档
│   ├── ModbusProtocol.md                # Modbus 协议说明
│   └── UserManual.md                    # 用户手册
│
└── tools/
    └── ModbusSimulator/                 # Modbus 从站模拟器
        └── Program.cs
```

### 2.3 架构分层

```
┌─────────────────────────────────────────────────────┐
│                    Views (XAML)                       │
│   Dashboard │ Trend │ Calibration │ Config │ Control │
├─────────────────────────────────────────────────────┤
│                  ViewModels                           │
│   数据绑定 │ 命令处理 │ 导航 │ 状态管理               │
├─────────────────────────────────────────────────────┤
│                  Services                             │
│   DataService │ HistoryService │ ExportService        │
├─────────────────────────────────────────────────────┤
│                Communication                          │
│   ModbusRtuService │ SerialPortManager │ Simulator   │
├─────────────────────────────────────────────────────┤
│                  Models                               │
│   SensorData │ DacCalibration │ SpanConfig │ ...     │
└─────────────────────────────────────────────────────┘
```

---

## 3. 核心模块设计

### 3.1 通信模块 (UMF.Core/Communication)

#### 3.1.1 接口定义

```csharp
/// <summary>
/// Modbus 通信服务接口
/// </summary>
public interface IModbusService : IAsyncDisposable
{
    /// <summary>连接状态</summary>
    bool IsConnected { get; }

    /// <summary>连接状态变化事件</summary>
    event EventHandler<bool> ConnectionStateChanged;

    /// <summary>建立连接</summary>
    Task ConnectAsync(ConnectionConfig config, CancellationToken ct = default);

    /// <summary>断开连接</summary>
    Task DisconnectAsync();

    // === FC03: 读保持寄存器 ===

    /// <summary>读取传感器实时数据 (流量/温度/压力)</summary>
    Task<SensorData> ReadSensorDataAsync(CancellationToken ct = default);

    /// <summary>读取 DAC 校准值</summary>
    Task<DacCalibration> ReadDacCalibrationAsync(CancellationToken ct = default);

    /// <summary>读取量程配置</summary>
    Task<SpanConfig> ReadSpanConfigAsync(CancellationToken ct = default);

    /// <summary>读取累积流量</summary>
    Task<ulong> ReadCumulativeFlowAsync(CancellationToken ct = default);

    // === FC01: 读线圈 ===

    /// <summary>读取全部线圈状态</summary>
    Task<CoilState> ReadCoilsAsync(CancellationToken ct = default);

    // === FC05: 写单个线圈 ===

    /// <summary>写线圈</summary>
    Task WriteCoilAsync(ushort address, bool value, CancellationToken ct = default);

    // === FC10: 写多个保持寄存器 ===

    /// <summary>写 DAC 校准值 (需校准模式使能)</summary>
    Task WriteDacCalibrationAsync(ushort zeroValue, ushort fullValue, CancellationToken ct = default);

    /// <summary>写量程配置 (需校准模式使能)</summary>
    Task WriteSpanConfigAsync(float spanLo, float spanHi, CancellationToken ct = default);
}
```

#### 3.1.2 数据模型

```csharp
/// <summary>
/// 传感器实时数据
/// </summary>
public class SensorData
{
    public float FlowRate { get; init; }       // 瞬时流量
    public float Temperature { get; init; }    // 介质温度 (°C)
    public float Pressure { get; init; }       // 介质压力
    public DateTime Timestamp { get; init; }   // 采集时间戳
}

/// <summary>
/// DAC 校准数据
/// </summary>
public class DacCalibration
{
    public ushort ZeroValue { get; init; }     // 4mA 零点 PWM 值
    public ushort FullValue { get; init; }     // 20mA 满度 PWM 值
}

/// <summary>
/// 量程配置
/// </summary>
public class SpanConfig
{
    public float SpanLo { get; init; }         // 4mA 对应工程量程下限
    public float SpanHi { get; init; }         // 20mA 对应工程量程上限
}

/// <summary>
/// 线圈状态
/// </summary>
public class CoilState
{
    public bool CalEnabled { get; init; }      // DAC 校准使能
    public bool ForceOutput4mA { get; init; }  // 强制 4mA
    public bool ForceOutput20mA { get; init; } // 强制 20mA
    public bool FlowClearCmd { get; init; }    // 累积清零
    public bool FlowResetCmd { get; init; }    // 模组复位
    public bool PassiveReadMode { get; init; } // 被动读取模式
}
```

#### 3.1.3 寄存器解析 — 关键注意点

**float 解析** (Modbus 保持寄存器 → IEEE 754 float)：
```
固件字节序: [str[1], str[0], str[3], str[2]]
即: [HiWord_Hi, HiWord_Lo, LoWord_Hi, LoWord_Lo]
NModbus4 读出 2 个 ushort 后需按 Big-Endian 重组为 float
```

**uint64 累积流量** (特殊字节序)：
```
固件字节序: [40>>8, 32>>8, 56>>8, 48>>8, 8>>8, 0>>8, 24>>8, 16>>8]
即按 8bit 为单位交错排列，非标准大端序
解析时需要按固件源码中的位移顺序还原
```

### 3.2 数据采集服务 (UMF.Core/Services/DataService)

```csharp
/// <summary>
/// 数据采集服务 — 定时轮询 Modbus 寄存器
/// </summary>
public interface IDataService : IAsyncDisposable
{
    /// <summary>最新传感器数据</summary>
    SensorData LatestData { get; }

    /// <summary>数据更新事件 (每次轮询触发)</summary>
    event EventHandler<SensorData> DataUpdated;

    /// <summary>连接丢失事件</summary>
    event EventHandler<Exception> ConnectionLost;

    /// <summary>启动定时采集</summary>
    Task StartAsync(int intervalMs = 1000, CancellationToken ct = default);

    /// <summary>停止采集</summary>
    Task StopAsync();

    /// <summary>采集是否运行中</summary>
    bool IsRunning { get; }
}
```

**轮询策略**：
- 默认 1 秒间隔，用户可调 (500ms~5000ms)
- 分组读取减少通信次数：
  - 组 1: 寄存器 0~5 (流量/温度/压力) — 每次都读
  - 组 2: 寄存器 40~47 (累积流量) — 每 5 秒读一次
  - 组 3: 寄存器 20~21, 30~33 (DAC/Span) — 进入对应页面时读取
  - 组 4: 线圈 0~5 — 进入控制页时读取
- 异常重试：单次失败不中断，连续 3 次失败触发连接丢失事件
- 通信超时保护：每次请求设置 1s 超时，避免阻塞 UI

### 3.3 历史数据服务 (UMF.Core/Services/HistoryService)

**SQLite 表设计**：

```sql
-- 传感器数据记录
CREATE TABLE SensorRecords (
    Id          INTEGER PRIMARY KEY AUTOINCREMENT,
    Timestamp   TEXT NOT NULL,         -- ISO 8601 格式
    FlowRate    REAL NOT NULL,         -- 瞬时流量
    Temperature REAL,                  -- 温度
    Pressure    REAL,                  -- 压力
    Cumulative  INTEGER                -- 累积流量
);

-- 操作日志
CREATE TABLE OperationLogs (
    Id          INTEGER PRIMARY KEY AUTOINCREMENT,
    Timestamp   TEXT NOT NULL,
    Operation   TEXT NOT NULL,         -- 操作类型
    Detail      TEXT,                  -- 操作详情
    Result      TEXT                   -- 结果 (Success/Failed)
);

-- 索引
CREATE INDEX idx_sensor_timestamp ON SensorRecords(Timestamp);
```

**记录策略**：
- 实时数据按采集间隔自动存入 SQLite
- 支持手动启停记录
- 数据自动清理：超过 30 天的记录自动归档/删除（可配）
- 查询接口支持时间范围筛选和分页

### 3.4 数据导出服务

- **Excel 导出**: 选中时间范围的数据导出为 `.xlsx`
- **CSV 导出**: 轻量级文本格式，兼容第三方工具
- **PDF 报告**: 生成包含图表的检测报告（可选，Phase 3）

---

## 4. UI 设计

### 4.1 主窗口布局

```
┌─────────────────────────────────────────────────────────────┐
│  UMF 超声波流量监控系统                          ─  □  ✕   │
├──────────┬──────────────────────────────────────────────────┤
│          │                                                  │
│  📊 监控  │  ┌─ 主内容区 ─────────────────────────────────┐  │
│  📈 趋势  │  │                                          │  │
│  🔧 校准  │  │         (当前选中页面的内容)               │  │
│  ⚙️ 配置  │  │                                          │  │
│  🎮 控制  │  │                                          │  │
│  🧪 模拟  │  │                                          │  │
│  📁 历史  │  │                                          │  │
│          │  └──────────────────────────────────────────┘  │
│          │                                                  │
├──────────┴──────────────────────────────────────────────────┤
│  状态栏: COM3 @ 9600 | 从站: 2 | 采集: 1s | 已连接 ●       │
└─────────────────────────────────────────────────────────────┘
```

- 左侧导航栏：图标 + 文字，可折叠
- 顶部标题栏：连接状态指示灯
- 底部状态栏：串口信息、从站地址、采集间隔、连接状态
- 主内容区：根据导航切换

### 4.2 监控页 (DashboardView)

```
┌──────────────────────────────────────────────────────┐
│                                                      │
│   ┌─── 瞬时流量 ───────────────────────┐            │
│   │                                    │            │
│   │        ████████████████            │            │
│   │        █              █            │            │
│   │        █   1234.56    █  m³/h      │            │
│   │        █              █            │            │
│   │        ████████████████            │            │
│   │                                    │            │
│   └────────────────────────────────────┘            │
│                                                      │
│   ┌── 温度 ────┐  ┌── 压力 ────┐  ┌── 累积流量 ──┐ │
│   │  25.3 °C  │  │  0.6 MPa  │  │  12345.67   │ │
│   │  ▲ 稳定   │  │  ▼ 下降   │  │  m³         │ │
│   └───────────┘  └───────────┘  └─────────────┘ │
│                                                      │
│   ┌── DAC 输出 ─────────────────────────────────┐   │
│   │  当前: 12.5 mA  │  占空比: 62.5%           │   │
│   │  [████████████████░░░░░░░░]  62.5%          │   │
│   └──────────────────────────────────────────────┘   │
│                                                      │
└──────────────────────────────────────────────────────┘
```

**核心元素**：
- 瞬时流量：大字数值 + 单位 + 圆弧仪表盘控件 (GaugeControl)
- 温度/压力：数值卡片 + 迷你趋势线 + 变化方向箭头
- 累积流量：数值卡片 + 单位
- DAC 输出：电流值 + 进度条可视化
- 通信状态：在线/离线指示 + 信号质量

### 4.3 趋势图页 (TrendView)（暂时不需要实现）

```
┌──────────────────────────────────────────────────────┐
│  时间范围: [1h ▼]  变量: [✓流量 ✓温度 ✓压力]        │
│                                                      │
│  ┌── 流量趋势 ──────────────────────────────────┐   │
│  │ 1400 ┤                                      │   │
│  │ 1200 ┤   ╭──╮    ╭──╮                       │   │
│  │ 1000 ┤──╯    ╰──╯    ╰──                    │   │
│  │  800 ┤                                      │   │
│  │      └──┬────┬────┬────┬────┬──             │   │
│  │        10:00 10:15 10:30 10:45              │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌── 温度趋势 ──────────────────────────────────┐   │
│  │  ...                                           │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  [暂停采集]  [导出图片]  [导出数据]                  │
└──────────────────────────────────────────────────────┘
```

**功能**：
- 多变量独立 Y 轴图表
- 时间范围选择：实时 / 1h / 6h / 24h / 自定义
- 鼠标悬停显示精确数值
- 实时自动滚动或锁定时间窗口
- 图表截图导出为 PNG

### 4.4 DAC 校准页 (CalibrationView)

```
┌──────────────────────────────────────────────────────┐
│  ⚠️ DAC 校准模式                                     │
│                                                      │
│  校准状态: [未使能]  [进入校准模式]                   │
│                                                      │
│  ┌── 零点校准 (4mA) ────────────────────────────┐   │
│  │  当前 PWM 值: [  8000  ]                     │   │
│  │  万用表读数:  [  4.00  ] mA                   │   │
│  │  [强制输出 4mA]  [写入零点值]                 │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌── 满度校准 (20mA) ───────────────────────────┐   │
│  │  当前 PWM 值: [ 40000  ]                     │   │
│  │  万用表读数:  [ 20.00  ] mA                   │   │
│  │  [强制输出 20mA] [写入满度值]                 │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  [退出校准模式]  [恢复默认]                          │
└──────────────────────────────────────────────────────┘
```

**操作流程**：
1. 点击"进入校准模式"→ FC05 写线圈 0 = ON
2. 点击"强制输出 4mA"→ FC05 写线圈 1 = ON → 万用表测量实际电流
3. 调整 PWM 值 → FC10 写入寄存器 20 → 反复调节至 4.00mA
4. 同理校准 20mA 满度
5. 完成后"退出校准模式"→ FC05 写线圈 0 = OFF

**安全保护**：
- 关键操作前弹出确认对话框
- 自动检测是否在校准模式下才允许写入 DAC 值
- 退出页面时自动退出校准模式

### 4.5 参数配置页 (ConfigView)

```
┌──────────────────────────────────────────────────────┐
│  ┌── 量程配置 ──────────────────────────────────┐   │
│  │  4mA 对应流量: [   0.00   ]                  │   │
│  │  20mA 对应流量: [ 100.00   ]                 │   │
│  │  [读取当前值]  [写入]                         │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌── 连接配置 ──────────────────────────────────┐   │
│  │  从站地址: [  2  ] (1~247)                   │   │
│  │  波特率:   [ 9600 ▼]                         │   │
│  │  采集间隔: [ 1000 ▼] ms                      │   │
│  └──────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────┘
```

### 4.6 运行控制页 (ControlView)

```
┌──────────────────────────────────────────────────────┐
│  ┌── 线圈状态 ──────────────────────────────────┐   │
│  │  ● DAC 校准:  OFF                            │   │
│  │  ● 强制 4mA:  OFF                            │   │
│  │  ● 强制 20mA: OFF                            │   │
│  │  ● 被动模式:  ON                             │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌── 操作 ──────────────────────────────────────┐   │
│  │  [累积量清零]  [模组复位]                     │   │
│  │  [切换主动模式]  [切换被动模式]               │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ⚠️ 危险操作需二次确认                               │
└──────────────────────────────────────────────────────┘
```

### 4.7 历史数据页 (HistoryView)

```
┌──────────────────────────────────────────────────────┐
│  起始: [2026-04-28 00:00] ~ 终止: [2026-04-28 23:59]│
│  [查询]  [导出 Excel]  [导出 CSV]                    │
│                                                      │
│  ┌── 数据表格 ──────────────────────────────────┐   │
│  │ 时间         │ 流量    │ 温度  │ 压力  │ 累积 │   │
│  │ 10:00:00    │ 1234.5 │ 25.3  │ 0.60 │ 1234 │   │
│  │ 10:00:01    │ 1235.0 │ 25.3  │ 0.60 │ 1234 │   │
│  │ 10:00:02    │ 1236.2 │ 25.4  │ 0.61 │ 1235 │   │
│  │ ...          │ ...    │ ...   │ ...  │ ...  │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  [◀ 上一页]  第 1/50 页  [下一页 ▶]                 │
└──────────────────────────────────────────────────────┘
```

### 4.8 模拟控制页 (SimulationView)

```
┌──────────────────────────────────────────────────────┐
│  ⚠️ 模拟模式 — 用于调试和测试，不影响传感器真实数据  │
│                                                      │
│  ┌── 总开关 ────────────────────────────────────┐   │
│  │  模拟模式:  [OFF ● │ ON]                     │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌── 模拟瞬时流量 ──────────────────────────────┐   │
│  │  设定值: [  1234.56  ] m³/h                   │   │
│  │  [读取当前]  [写入]                            │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌── 模拟温度 ──────────────────────────────────┐   │
│  │  设定值: [  25.30  ] °C                       │   │
│  │  [读取当前]  [写入]                            │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌── 模拟累积流量 ──────────────────────────────┐   │
│  │  设定值: [  12345.678  ] m³                   │   │
│  │  [读取当前]  [写入]                            │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  提示: 开启总开关后，OLED 和 DAC 输出将跟随模拟值  │
└──────────────────────────────────────────────────────┘
```

**操作流程**：
1. 设置模拟流量/温度/累积值 → FC06 写入寄存器 40051~40056
2. 开启总开关 → FC06 写入寄存器 40049 = 1
3. 监控页实时显示模拟值，DAC 输出对应模拟流量
4. 关闭总开关 → FC06 写入寄存器 40049 = 0，恢复真实数据

### 4.9 主题与样式

- 配色方案：深色主题 (工业风格) + 浅色主题切换
- 主色调：`#0078D4` (微软蓝) / `#00B4D8` (科技蓝)
- 警告色：`#FF6B35` (橙) / `#E63946` (红)
- 正常色：`#2EC4B6` (绿)
- 字体：`Microsoft YaHei UI` / `Segoe UI`
- 圆角：4px 统一
- 间距：8px 网格系统

---

## 5. 开发阶段规划

### Phase 1: 基础框架 (预计 3 天)

| # | 任务 | 产出 | 工时 |
|---|---|---|---|
| 1.1 | 创建解决方案，搭建项目结构 | `.sln` + 项目骨架 | 2h |
| 1.2 | 配置 DI 容器，注册服务 | `App.xaml.cs` 启动配置 | 2h |
| 1.3 | 实现主窗口框架 + 导航 | 左侧导航 + 页面切换 | 4h |
| 1.4 | 实现串口管理器 | 枚举/打开/关闭串口 | 3h |
| 1.5 | 实现 `IModbusService` 接口 | NModbus4 集成 | 4h |
| 1.6 | 实现 Modbus 模拟器 | 开发阶段无硬件测试 | 4h |
| 1.7 | 单元测试：寄存器解析 | float/uint64 转换测试 | 3h |

**里程碑**: 无硬件条件下，通过模拟器完成通信层验证。

### Phase 2: 核心功能 (预计 5 天)

| # | 任务 | 产出 | 工时 |
|---|---|---|---|
| 2.1 | 实时监控页 UI + 数据绑定 | DashboardView 完成 | 6h |
| 2.2 | GaugeControl 圆弧仪表控件 | 自定义 WPF 控件 | 4h |
| 2.3 | DataService 定时采集服务 | 轮询 + 事件推送 | 4h |
| 2.4 | 趋势图页 — LiveCharts2 集成 | TrendView 完成 | 6h |
| 2.5 | 连接设置页 | 串口选择/参数配置/保存 | 3h |
| 2.6 | 运行控制页 — 线圈读写 | ControlView 完成 | 4h |
| 2.7 | 全局异常处理 + 日志 | Serilog 集成 | 2h |

**里程碑**: 连接真实传感器，实时显示流量/温度/压力数据。

### Phase 3: 校准与配置 (预计 3 天)

| # | 任务 | 产出 | 工时 |
|---|---|---|---|
| 3.1 | DAC 校准页 UI + 流程 | CalibrationView 完成 | 6h |
| 3.2 | 参数配置页 — 量程读写 | ConfigView 完成 | 4h |
| 3.3 | 安全保护机制 | 二次确认 + 校准模式检测 | 3h |
| 3.4 | 操作日志记录 | 写入 SQLite OperationLogs | 2h |

**里程碑**: 完成完整的校准和配置工作流。

### Phase 4: 数据管理 (预计 3 天)

| # | 任务 | 产出 | 工时 |
|---|---|---|---|
| 4.1 | SQLite 历史数据存储 | HistoryService 实现 | 4h |
| 4.2 | 历史数据查询页 | HistoryView + 分页 + 筛选 | 5h |
| 4.3 | Excel 导出 (ClosedXML) | 导出功能 | 3h |
| 4.4 | CSV 导出 | 简易文本导出 | 1h |
| 4.5 | 数据自动清理策略 | 定期归档/删除 | 2h |

**里程碑**: 完整的数据记录和导出功能。

### Phase 5: 优化与发布 (预计 3 天)

| # | 任务 | 产出 | 工时 |
|---|---|---|---|
| 5.1 | UI 打磨 — 动画 + 主题切换 | 视觉优化 | 4h |
| 5.2 | 多语言支持 (中/英) | ResourceManager | 4h |
| 5.3 | 连接配置持久化 | JSON 文件保存用户设置 | 2h |
| 5.4 | 打包发布 — 单文件发布 | `.exe` 无需安装 | 2h |
| 5.5 | 用户手册编写 | `docs/UserManual.md` | 3h |
| 5.6 | 集成测试 + Bug 修复 | 测试报告 | 4h |

**里程碑**: 可发布版本 v1.0。

---

## 6. 关键技术难点与对策

### 6.1 累积流量 uint64 特殊字节序

**问题**: 固件中累积流量的字节序非标准排列：
```c
TxBuffer[i] = Cumulativeflow >> 40;  // byte 0
TxBuffer[i] = Cumulativeflow >> 32;  // byte 1
TxBuffer[i] = Cumulativeflow >> 56;  // byte 2
TxBuffer[i] = Cumulativeflow >> 48;  // byte 3
TxBuffer[i] = Cumulativeflow >> 8;   // byte 4
TxBuffer[i] = Cumulativeflow >> 0;   // byte 5
TxBuffer[i] = Cumulativeflow >> 24;  // byte 6
TxBuffer[i] = Cumulativeflow >> 16;  // byte 7
```

**对策**: 在 `ModbusRegisterConverter` 中实现专用解析方法，编写单元测试覆盖边界值。

### 6.2 实时图表性能

**问题**: 1 秒采集间隔，24 小时 = 86400 个数据点，WPF 渲染压力大。

**对策**:
- LiveCharts2 内置数据虚拟化，仅渲染可见区间
- 超过 3600 点时自动降采样 (取最大/最小/平均)
- 使用 `ObservableCollection` 替代频繁刷新

### 6.3 Modbus 通信可靠性

**问题**: RS-485 总线可能受工业环境电磁干扰，导致 CRC 错误或超时。

**对策**:
- NModbus4 内置 CRC 校验，自动重试
- DataService 实现连续 N 次失败才判定断线
- 通信异常时 UI 显示最后有效值 + 离线标记
- 所有写操作记录日志，便于追溯

### 6.4 校准流程安全性

**问题**: 误操作可能导致 DAC 输出异常电流，影响下游设备。

**对策**:
- 校准页必须先"进入校准模式"才激活写入按钮
- 强制输出前弹出确认对话框，说明当前操作和影响
- PWM 值写入前验证范围合理性
- 页面关闭时自动退出校准模式

---

## 7. NuGet 依赖清单

| 包名 | 版本 | 用途 |
|---|---|---|
| `CommunityToolkit.Mvvm` | 8.x | MVVM 框架 (Source Generator) |
| `NModbus` | 4.x 或 `FluentModbus` 2.x | Modbus RTU 通信 |
| `System.IO.Ports` | 8.x | 串口通信 |
| `LiveChartsCore.SkiaSharpView.WPF` | 2.x | 实时图表 |
| `Microsoft.Extensions.DependencyInjection` | 8.x | 依赖注入 |
| `Microsoft.Extensions.Hosting` | 8.x | 泛型主机 |
| `Serilog` + `Serilog.Sinks.File` | 3.x / 5.x | 日志 |
| `Microsoft.Data.Sqlite` | 8.x | SQLite 数据库 |
| `ClosedXML` | 0.102.x | Excel 导出 |
| `CommunityToolkit.WinUI.Converters` | 8.x (WPF 兼容) | 常用值转换器 |

---

## 8. 测试策略

| 测试类型 | 范围 | 工具 |
|---|---|---|
| 单元测试 | ModbusRegisterConverter, DataService, HistoryService | xUnit + Moq |
| 模拟器集成测试 | IModbusService 完整读写流程 | ModbusSimulator (自研) |
| 硬件集成测试 | 真实传感器通信验证 | USB-RS485 + UMF 传感器 |
| UI 自动化测试 | 关键用户流程 (连接/监控/校准) | 手动测试 |
| 性能测试 | 24h 长时间运行稳定性 | 内存/CPU 监控 |

---

## 9. 风险评估

| 风险 | 概率 | 影响 | 缓解措施 |
|---|---|---|---|
| RS-485 转换器兼容性问题 | 中 | 高 | 提前测试多种 USB-RS485 芯片 (CH340/FT232/CP2102) |
| 累积流量字节序解析错误 | 高 | 中 | 参照固件源码编写精确测试用例，用已知值验证 |
| WPF 图表大数据量卡顿 | 中 | 中 | 降采样策略 + LiveCharts2 虚拟化 |
| .NET 8 运行时在旧 Windows 不兼容 | 低 | 高 | 发布为独立部署 (self-contained)，目标框架 `net8.0-windows` |
| Modbus 库 Bug | 低 | 高 | 通信层抽象接口，可替换实现 |

---

## 10. 交付物

| 交付物 | 格式 | 说明 |
|---|---|---|
| 可执行程序 | `UMF.HostApp.exe` | 单文件发布，无需安装 .NET 运行时 |
| 用户手册 | PDF / CHM | 操作说明 + 常见问题 |
| 源代码 | Git 仓库 | 含完整提交历史 |
| 测试报告 | Markdown | 测试用例 + 测试结果 |
| Modbus 协议文档 | Markdown | 寄存器地址表 + 通信示例 |
