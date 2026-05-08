# 瞬时流量异常值过滤 — 累加器去极值方案

> 状态: **已实现** (v2.1.0)
> 实现: 去最大值单极值方案 (K=1), 内联于 `bsp_usart.c`
> 目标: 去除 UART1 BCD 通信毛刺引入的异常流量值

## 1. 问题背景

UFL-1A 超声波流量模组通过 USART1 (DMA + IDLE 中断) 以自定义 BCD 协议上报瞬时流量。
偶发的通信毛刺或传感器瞬态噪声会导致解析出的 `FlowRateValue.num` 出现
明显偏离正常范围的异常值，进而影响 DAC 4~20mA 输出和 OLED 显示。

## 2. 实现方案：累加器去最大值 (Trimmed Mean, K=1)

### 2.1 核心思路

收集 N 个连续样本，追踪累加和与最大值。
凑满 N 个后，从累加和中扣除最大值，对剩余 N-1 个样本取均值。

利用流量值恒 ≥ 0 的物理特性，`s_flt_max` 零初始化后首个正样本自动更新，
**无需显式 init 函数和哨兵常量**，节省 Flash。

### 2.2 为什么只去掉最大值

BCD 通信毛刺产生的异常值几乎都是**偏大**的（垃圾数据解码为巨大数值），
极少出现异常偏小。去掉最大值即可过滤绝大多数异常 spike。

同时，Flash 代码区空间极为紧张（64KB MCU，仅剩 ~200 字节），
去掉 min 追踪可省去一次 float 比较、一次 float 减法和 first-sample 初始化检查，
节省 ~60 字节 Flash。

### 2.3 参数

| 参数 | 值 | 说明 |
|------|----|------|
| 窗口大小 N | 10 | 累计 10 个样本后输出一次 |
| 去极值数 | 1 (最大值) | 从累加和中扣除 1 个最大值 |
| 有效样本数 | N - 1 = 9 | 最终参与均值的样本数 |

### 2.4 内存开销

```
static float    s_flt_sum;       // 累加和          4 字节
static float    s_flt_max;       // 当前轮最大值    4 字节
static uint8_t  s_flt_count;     // 样本计数        1 字节
static float    s_flt_result;    // 本轮滤波结果    4 字节
static uint8_t  s_flt_valid;     // 结果有效标志    1 字节
──────────────────────────────────────────────
总计                              14 字节 (RAM)
Flash 代码                        ~120 字节
```

### 2.5 算法流程

```
输入: FlowRateValue.num (每次 BCD 解析完成后触发)

1. s_flt_sum += sample
2. if sample > s_flt_max: s_flt_max = sample
3. s_flt_count++
4. if s_flt_count >= N (10):
     s_flt_result = (s_flt_sum - s_flt_max) / (N - 1)
     s_flt_valid  = 1
     清零: sum=0, count=0, max=0
5. 输出: s_flt_result (仅在 valid 时使用)
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

### 3.3 初始化

无需显式调用 init 函数。所有 static 变量通过 C 语言零初始化机制自动初始化，
首个正流量样本自动更新 `s_flt_max`。

## 4. 数据流

```
UFL-1A → USART1 DMA+IDLE → BCD 解码 → FlowRateValue.num
                                              │
                              flow_filter_feed(FlowRateValue.num)
                                              │
                              累加器 (N=10, 去最大值)
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
| 首次上电，未凑满 N 个 | `s_flt_valid = 0`，回退使用原始 `FlowRateValue.num` |
| 模拟模式开启 | 直接返回 `s_sim_flow_rate`，滤波器不参与 |
| 标定/强制 DAC 模式 | DAC 换算被 gate 跳过，滤波器持续喂入不受影响 |
| 模块通信中断 | 无新 BCD 数据，滤波器不触发，保持上一轮有效结果 |
| 菜单激活期间 | 显示暂停但数据持续喂入，不丢失样本 |
| 全零流量 | `s_flt_max = 0.0f`，sum/count 正常工作，结果为 0 |

## 6. 延迟

假设 UFL-1A 数据帧间隔约 500ms（被动模式 Timer3Uart1TimeBase10ms >= 50）：

- 窗口 N=10，需累计 ~5 秒
- 首次输出延迟: ~5 秒
- 后续每 ~5 秒更新一次结果
- 两轮之间滤波输出保持上一轮结果（零阶保持）

**降低延迟**: 将 `FLOW_FILTER_N` 改为 5，延迟降至 ~2.5 秒。

## 7. 后续扩展方向 (需要更大 Flash 的芯片)

- **K=2 双极值追踪**: 同时去掉最大 2 个和最小 2 个，抗干扰更强
- **与 filter_time / damping_time 参数联动**: 当前这两个参数已定义但未使用
- **自适应阈值**: 偏离均值超过 X% 即视为异常的二次筛选
- **统计诊断**: 通过 Modbus 寄存器暴露被剔除样本数
