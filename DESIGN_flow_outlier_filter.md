# 瞬时流量异常值过滤 — 滑动窗口去极值 + 一阶低通

> 状态: **已实现** (v2.3.3)
> 实现: 可配置滑动窗口去最大值 + 一阶低通，内联于 `bsp_usart.c`
> 目标: 去除 UART1 BCD 通信毛刺引入的异常流量值

## 1. 问题背景

UFL-1A 超声波流量模组通过 USART1 (DMA + IDLE 中断) 以自定义 BCD 协议上报瞬时流量。
偶发的通信毛刺或传感器瞬态噪声会导致解析出的 `FlowRateValue.num` 出现
明显偏离正常范围的异常值，进而影响 DAC 4~20mA 输出和 OLED 显示。

## 2. 实现方案：滑动窗口去最大值 + 一阶低通

### 2.1 核心思路

收集 N 个连续样本形成环形窗口，凑满后扣除窗口内最大值，对剩余 N-1 个样本取均值；
再将窗口结果送入一阶低通，抑制正常流量的高频抖动。

滤波窗口使用固定 10 点数组，不动态分配内存。窗口点数、低通时间常数和采样间隔
均由参数存储提供，可通过 Modbus 修改。

### 2.2 为什么只去掉最大值

BCD 通信毛刺产生的异常值几乎都是**偏大**的（垃圾数据解码为巨大数值），
极少出现异常偏小。去掉最大值即可过滤绝大多数异常 spike。

同时，Flash 代码区空间极为紧张（64KB MCU，仅剩 ~200 字节），
去掉 min 追踪可省去一次 float 比较、一次 float 减法和 first-sample 初始化检查，
节省 ~60 字节 Flash。

### 2.3 参数

| 参数 | 值 | 说明 |
|------|----|------|
| 窗口大小 N | 2~10 (默认 10) | Modbus 40125，修改后清空旧窗口并重新累计 |
| 去极值数 | 1 (最大值) | 从累加和中扣除 1 个最大值 |
| 有效样本数 | N - 1 = 9 | 最终参与均值的样本数 |
| 一阶低通时间常数 τ | 0.1~100.0s (默认 1.0s) | Modbus 40071~40072 (`filter_time`) |
| 采样间隔 dt | 100~60000ms (默认 500ms) | Modbus 40126 (`sample_interval_ms`) |

低通计算公式：

```text
y[n] = y[n-1] + α × (x[n] - y[n-1])
α = dt / (τ + dt)
```

### 2.4 内存开销

```
static float    s_flt_window[10]; // 固定最大窗口   40 字节
static uint8_t  s_flt_write_index; // 环形写入位置    1 字节
static uint8_t  s_flt_count;       // 样本计数        1 字节
static uint8_t  s_flt_window_size;  // 当前窗口点数    1 字节
static float    s_flt_result;        // 低通结果        4 字节
static uint8_t  s_flt_valid;         // 结果有效标志    1 字节
static float    s_flt_time_constant; // 当前 τ          4 字节
static uint16_t s_flt_sample_interval_ms; // 当前 dt     2 字节
──────────────────────────────────────────────
总计约                              56 字节 (RAM)
```

### 2.5 算法流程

```
输入: FlowRateValue.num (每次 BCD 解析完成后触发)

1. 将 sample 写入固定容量环形窗口，窗口容量取 `filter_window_count` (N)
2. 窗口未填满时，使用当前 sample 作为窗口输出；填满后计算 `(sum - max)/(N-1)`
3. 使用 `sample_interval_ms` 和 `filter_time` 计算 α，执行一阶低通
4. 后续每收到一个新样本，覆盖最旧样本并更新低通结果
```

## 3. 实现位置

所有滤波逻辑作为 `static` 函数/变量内联于 `BSP/bsp_usart.c`，无独立文件。

### 3.1 喂入点

`Uart1_Receive_Function()` 内，两处 BCD 解析赋值 `FlowRateValue.num` 之后：

```c
FlowRateValue.num = BCDTOInt(flowrate);           // 0x1b 帧类型
flow_filter_feed(FlowRateValue.num);

FlowRateValue.num = BCDTOInt(flowrate) / 100.0f;  // 0x0b 帧类型
flow_filter_feed(FlowRateValue.num);
```

### 3.2 输出点

`effective_flow_rate()` 内，real 数据路径优先返回滤波结果：

```c
float effective_flow_rate(void)
{
    if (sim_is_active()) return s_sim_flow_rate.num;
    if (s_flt_valid) return s_flt_result;
    return FlowRateValue.num;  /* 滤波器未就绪时回退到原始值 */
}
```

display（S01/S02）和 DAC 换算两处调用 `effective_flow_rate()` 均自动获得滤波值。
模拟流量路径 (`sim_is_active()`) 完全不经过滤波器，不受影响。

### 3.3 参数修改

滑动窗口点数通过保持寄存器 40125 (`filter_window_count`) 读写，范围为 2~10；
一阶低通时间常数使用已有保持寄存器 40071~40072 (`filter_time`)，范围为 0.1~100.0 秒；
UFL-1A 被动采样间隔使用保持寄存器 40126 (`sample_interval_ms`)，范围为 100~60000ms。
任一参数变化后，滤波器清空已有样本和旧结果，从下一帧开始重新累计，避免新旧参数混用。

### 3.4 初始化

无需显式调用 init 函数。所有 static 变量通过 C 语言零初始化机制自动初始化，
首个样本初始化低通结果；窗口填满后再启用去最大值均值。

## 4. 数据流

```
UFL-1A → USART1 DMA+IDLE → BCD 解码 → FlowRateValue.num
                                              │
                              flow_filter_feed(FlowRateValue.num)
                                              │
                              滑动窗口 (N=filter_window_count, 去最大值)
                                              │
                              一阶低通 (τ=filter_time, dt=sample_interval_ms)
                                              │
                              effective_flow_rate()
                                ├─ sim_is_active() → s_sim_flow_rate (不受影响)
                                ├─ s_flt_valid → s_flt_result (滤波值)
                                └─ fallback → FlowRateValue.num (原始值)
                                              │
                              → 信号链 × 仪表系数 × 介质系数 × 标定修正
                                              │
                              → DAC 换算 / OLED 显示
```

## 5. 边界情况

| 场景 | 处理方式 |
|------|----------|
| 首次上电，尚无样本 | `s_flt_valid = 0`，回退使用原始 `FlowRateValue.num` |
| 模拟模式开启 | 直接返回 `s_sim_flow_rate`，滤波器不参与 |
| 标定/强制 DAC 模式 | DAC 换算被 gate 跳过，滤波器持续喂入不受影响 |
| 模块通信中断 | 无新 BCD 数据，滤波器不触发，保持上一轮有效结果 |
| 菜单激活期间 | 显示暂停但数据持续喂入，不丢失样本 |
| 全零流量 | 窗口内最大值为 0，sum/count 正常工作，结果为 0 |
| 窗口点数修改 | 清空旧窗口、旧结果失效，按新点数重新累计 |
| 低通参数或采样间隔修改 | 清空窗口和低通状态，按新参数重新累计 |

## 6. 延迟

被动模式下 UFL-1A 数据帧间隔由 40126 配置，默认 500ms：

- 窗口 N=2~10，首次窗口结果延迟约 N×dt
- 默认 N=10、dt=500ms 时，首次窗口结果延迟约 5 秒
- 窗口填满后每收到一个新样本即更新结果
- 低通会在每个采样周期平滑更新输出

**降低延迟**: 通过 Modbus 将 40125 写为 5 或将 40126 写为更小的毫秒值。

## 7. 后续扩展方向 (需要更大 Flash 的芯片)

- **K=2 双极值追踪**: 同时去掉最大 2 个和最小 2 个，抗干扰更强
- **阻尼时间联动**: 当前 `damping_time` 参数仍由原有参数菜单维护，未接入本滤波链
- **自适应阈值**: 偏离均值超过 X% 即视为异常的二次筛选
- **统计诊断**: 通过 Modbus 寄存器暴露被剔除样本数
