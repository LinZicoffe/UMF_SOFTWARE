# 真实 Cortex-M3 指令的离线回归

本目录直接交叉编译仓库的生产 C 文件，再让 Unicorn 执行生成的 Arm 指令。**全部组当前有一项确认的生产缺陷，因此应返回失败；不能将它写成全部通过。** 运行快照见 [RESULTS.md](RESULTS.md)。不连接、烧录或控制硬件。

## 1. 先理解这层测试

原有 `tests/test_industrial_display.py` 把显示代码编译成电脑可执行程序，检查像素。新增链路为：

`真实 C 源码 → Arm GCC → ELF → Cortex-M3 指令执行 → C 断言 → JSON 报告/退出码`

GCC（GNU Compiler Collection，GNU 编译器集合）负责交叉编译；ELF（Executable and Linkable Format，可执行与可链接格式）保存机器码、数据、地址和符号。Unicorn 模拟 CPU（中央处理器），不自动提供 STM32 外设。测试替身只提供原始输入和记录输出，标定、消抖、协议解析、CRC（循环冗余校验）及滤波仍执行生产代码。

本文涉及 GPIO（通用输入输出引脚）、UART（通用异步收发器，串口）、HAL（硬件抽象层）和 CMSIS（Arm微控制器软件接口标准）；它们的边界按下文说明提供。

## 2. 工程核对与选择依据

| 项目 | 实际证据与配置 |
|---|---|
| 芯片 | `UMF.ioc` 的 `Mcu.CPN=STM32F103C8T6`；IAR `.ewp` 与 CubeIDE `.cproject` 均选择 F103C8 |
| 核心/指令 | Arm Cortex-M3 / Armv7-M / Thumb-2；72MHz；64KB Flash、20KB SRAM（静态随机存取存储器） |
| 大小端/浮点 | IAR `GEndianMode=0`；测试 ELF 为小端；Cortex-M3 无 FPU（浮点运算单元），使用软件浮点库 |
| 正式构建 | IAR `EWARM/Project.eww`；App 也支持 `STM32CubeIDE/.project`。AGENTS 中只列 IAR 8.32 的内容落后于工程，README 已记录 IAR 9.60.4 / CubeIDE 双工具链 |
| 正式优化 | IAR 配置 `CCOptLevel=3, CCOptStrategy=2`（高等级、偏尺寸）；CubeIDE Debug/Release C 配置均为 `-Os`（尺寸优化） |
| 宏 | `USE_HAL_DRIVER`、`STM32F103xB`；生产显示选择宏不由本测试改动 |
| 测试编译 | `-mcpu=cortex-m3 -mthumb -mlittle-endian -mfloat-abi=soft -Os -g3 -ffunction-sections -fdata-sections -fno-builtin`，不启用链接时优化；真实 HAL/CMSIS 头文件 |

已验证版本：Windows x64、Python **3.10.11**、Unicorn **2.1.4**、pyelftools **0.32**、STM32CubeIDE **1.19.0** 附带的 **Arm GCC 13.3.1 20240614 / GNU Tools for STM32 13.3.rel1.20240926-1715**。本机 IAR 编译器显示 9.60.4.438，但许可证检查 LMS001 失败，未用它构建测试。

官方依据（2026-10-23 核对）：

- [ST STM32F103x8/xB 数据手册](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf)：芯片核心与存储容量。
- [ST PM0056 编程手册](https://www.st.com/resource/en/programming_manual/pm0056-stm32f10xxx20xxx21xxxl1xxxx-cortexm3-programming-manual-stmicroelectronics.pdf)：Cortex-M3、Thumb-2 与存储模型。
- [Unicorn 2.1.4 Arm 模型号](https://github.com/unicorn-engine/unicorn/blob/2.1.4/include/unicorn/arm.h)：明确包含 `UC_CPU_ARM_CORTEX_M3`。
- [Unicorn 2.1.4 架构与模式接口](https://github.com/unicorn-engine/unicorn/blob/2.1.4/include/unicorn/unicorn.h)：Arm、Thumb-2、小端；本实现显式设置 Cortex-M3 模型，不依赖泛用 Arm 默认 CPU。
- [Unicorn 官方 FAQ](https://github.com/unicorn-engine/unicorn/blob/master/docs/FAQ.md#how-to-emulate-interrupts-or-ticks-with-unicorn)：CPU 模拟、执行限额及中断边界。
- [GCC Arm 选项](https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html)：`soft` 生成软件浮点库调用。**软件浮点库的机器指令也在模拟器中执行**。

因此当前选择 Unicorn。尚未核验它的完整 Cortex-M 异常嵌套、NVIC（嵌套向量中断控制器）、复位与周期精度；也没有 STM32 外设模型。QEMU、Renode 本次未安装或实测，不能宣称它们已适配本板。只有将来需要整板启动/外设联动时，才有必要进一步评估它们。

## 3. 安装与恢复环境

以下命令在 PowerShell 执行，每条单独运行。先进入仓库：

```powershell
Set-Location 'D:\works_shimu\UMF_SOFTWARE'
```

项目原来没有 Python 虚拟环境，因此创建独立 venv（虚拟环境），不改系统依赖：

```powershell
python -m venv .venv-firmware
```

安装锁定依赖；`--no-cache-dir` 避免本机 pip 用户缓存无写权限导致卡住：

```powershell
.\.venv-firmware\Scripts\python.exe -m pip install --no-cache-dir -r tests\firmware\requirements.txt
```

设置**本次 PowerShell 会话的变量**。下例搜索本机已有 CubeIDE；其他电脑替换安装根目录，脚本没有硬编码该路径：

```powershell
$armCc = (Get-ChildItem -LiteralPath 'D:\ST\STM32CubeIDE_1.19.0' -Filter arm-none-eabi-gcc.exe -Recurse | Select-Object -First 1).FullName
```

确认找到了目标编译器：

```powershell
& $armCc --version
```

也可以通过 `ARM_CC` 环境变量或 `PATH` 提供编译器；优先级为 `--cc`、`ARM_CC`、`PATH`。运行器同时需要该编译器同目录的 `arm-none-eabi-objdump` 和 `arm-none-eabi-readelf`。错误的电脑 GCC 会被拒绝。

需要重建损坏环境时，关闭占用该 venv 的进程，将旧目录改名保留，然后重新执行上述创建、安装命令：

```powershell
Rename-Item -LiteralPath .venv-firmware -NewName .venv-firmware.backup
```

完全断网恢复可先在联网时保存 wheel（Python 安装包）：

```powershell
.\.venv-firmware\Scripts\python.exe -m pip download --no-cache-dir -r tests\firmware\requirements.txt --dest tests\firmware\build\wheels
```

之后在新 venv 内离线安装；wheel 还需匹配电脑的操作系统和架构：

```powershell
.\.venv-firmware\Scripts\python.exe -m pip install --no-index --find-links tests\firmware\build\wheels -r tests\firmware\requirements.txt
```

只有首次安装/下载需要联网，编译和模拟运行不访问网络。编译器是外部工具，不包含在 Python wheel 中。

## 4. 运行

**全部测试**（当前应为 29 PASS / 1 FAIL，退出码 1，已知缺陷仍然存在）：

```powershell
.\.venv-firmware\Scripts\python.exe tests\firmware\run.py --cc $armCc --group all
```

正常回归组（明确排除 `known-defects`；当前 29 PASS / 0 FAIL）：

```powershell
.\.venv-firmware\Scripts\python.exe tests\firmware\run.py --cc $armCc --group regression
```

指定组；可多次传 `--group`，可选 `smoke`、`runner`、`keys`、`calibration`、`sensor`、`protocol`、`known-defects`：

```powershell
.\.venv-firmware\Scripts\python.exe tests\firmware\run.py --cc $armCc --group sensor --group keys
```

按函数名片段选择用例；可重复传 `--case`，匹配规则为“或”：

```powershell
.\.venv-firmware\Scripts\python.exe tests\firmware\run.py --cc $armCc --case threshold
```

单独重现已知缺陷：

```powershell
.\.venv-firmware\Scripts\python.exe tests\firmware\run.py --cc $armCc --group known-defects
```

列出匹配测试（会先构建 ELF，以实际链接符号为准）：

```powershell
.\.venv-firmware\Scripts\python.exe tests\firmware\run.py --cc $armCc --list
```

查看上一条命令的退出码：

```powershell
$LASTEXITCODE
```

另外核对运行器的报告保存契约（2项Python基础设施测试，使用模拟执行结果隔离I/O错误，不作为真实指令测试证据）：

```powershell
.\.venv-firmware\Scripts\python.exe -m unittest discover -s tests/firmware -p test_runner_errors.py -v
```

查看该命令退出码：

```powershell
$LASTEXITCODE
```

退出码：`0` 所选测试全部通过；`1` 任意断言/指令执行失败；`2` 环境、编译、装载、初始化或筛选错误。**不带 --group 时等同 all**。错误预期的负向演示已在接入过程中执行并恢复，详见运行记录。

默认产物在 `tests/firmware/build/latest/`；用 `--output` 指定独立目录可保留每次记录。每次重新构建，不复用旧 ELF。

## 5. 各部分为什么这样写

| 文件 | 职责/原因 |
|---|---|
| `run.py` | 构建、校验 Arm32 小端 ELF、装载所有 `PT_LOAD` 段，按 `p_vaddr` 写入文件数据并补零到 `p_memsz`，因此 `.data` 初值与 `.bss` 正确；不需要执行真正的复位启动代码 |
| `linker.ld` | 代码起点沿用 App 的 `0x08002400`，52KB；使用19KB应用 RAM，保留4KB测试栈；保留 `test_`/`probe_` 入口，垃圾回收不相关生产路径 |
| `smoke.c` | 先证明真实整数转换能运行，再证明 `.data/.bss` 初始化；`probe_` 是运行器的故障探针，不是假冒生产算法 |
| `doubles.c/h` | 原样采用 HAL（硬件抽象层）类型/引脚常量；提供可控 GPIO、串口发送结果、原始参数配置；记录输出先后、参数、次数和帧快照 |
| `cases.c` | 只设置输入、调用生产函数、检查独立期望；没有重写标定、解析或滤波算法 |

内存：映射实际64KB Flash 为只读/可执行、20KB SRAM 为可读写；栈顶 `0x20004C00`，8字节对齐。每例从对应 `test_` 入口开始，LR（链接寄存器，保存返回地址）指向 Flash 内 `0x0800FFF0` 哨兵，检查 PC（程序计数器）到达该地址且 SP（栈指针）恢复。该哨兵位于模拟 Flash 内，**不生成可烧录 App 包**。

每例新建 CPU/内存，默认最多100万条指令或2秒电脑时间；二者都不代表芯片运行了多少毫秒。可以通过 `--max-instructions`、`--timeout-ms` 改限额。非法读写、写只读 Flash、非法指令/异常、未返回或栈不平衡均失败。运行器自检分别验证了这些拒绝路径。

每例记录执行过的函数入口，且要求到达相应生产函数；DWARF（调试信息格式）还核对符号来自真实生产编译单元，防止同名替身顶替。报告保存 C 源码 SHA-256（安全哈希摘要）、ELF 摘要、工具版本、构建命令、反汇编与 ELF 属性。此项是入口和来源检查，**不是完整语句/分支覆盖率**。

## 6. 覆盖范围、期望来源和替身

| 真实模块 | 实际检查 | 输入边界/期望来源 |
|---|---|---|
| `BSP/ftoa.c` | 最小32位整数、正常/负小数、超范围显示 | 十进制数学、函数范围约定 |
| `BSP/key.c` | 消抖之前/达到阈值/长按、释放、单键、组合优先级、取事件清空、重启 | `key.h/c` 的20ms与释放触发接口契约；替代低有效 GPIO 输入 |
| `BSP/Src/cal_table.c` | 七节点、内插、区间两侧、端点夹取、量程偏移、未启用/无效量程 | `CALIBRATION_GUIDE.md`；使用独立手算表；getter 仅返回原始标定参数 |
| `BSP/bsp_usart.c`、`BSP/mystring.c` | BCD（二进制编码十进制）字段/单位、0~27字节短帧、错误校验/帧头/结尾、有效输入恢复、滤波/配置重置、采样阈值/改周期、超时/恢复、按键与采样交错 | 仓库 UFL-1A 字段说明、`DESIGN_flow_outlier_filter.md` 和独立常数；通过真实接收缓冲区喂入完整30字节帧，未替换解析器或 static 滤波函数 |
| 同上 Modbus 路径 | CRC-16/MODBUS check值 `0x4B37`、FC01完整响应、线圈字节顺序、零数量/超长静默、HAL_BUSY、发送完成方向恢复 | `UMF_Modbus_Protocol.md`、[Modbus §6.1](https://www.modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)；CRC固定参考值/帧由规范独立计算，不用生产函数生成期望 |

按键扫描和采样计数由测试显式推进10ms tick（节拍），不等待电脑时钟。`Time_Delay(n)` 在生产中是忙等待次数，本替身只记录 n，**没有把 n 当毫秒**。DMA（直接存储器访问）发送替身立即记录帧和返回状态；发送完成由用例显式调用真实 `HAL_UART_TxCpltCallback`，不会自动触发中断。

仅为真实发送完成回调映射 `0x40020000` 的4KB页，允许对 `DMA1->IFCR`（中断清除寄存器，`0x40020004`）的32位写入，其余该页访问拒绝。**它只是可读写的模拟内存，不会清中断、搬数据或改变外设状态**。GPIO/USART 地址只作为替身中的接口标识，不映射为可用外设。日志中方向引脚的 SET→发送→RESET 顺序是软件调用证据，不是引脚电平测量。

## 7. 定位失败与新增用例

先看控制台测试名和 C 断言行号，再打开对应 `cases.c`/`smoke.c`。`report.json` 保存断言、最后16条指令地址、执行函数、全程输出事件、IFCR写入以及未选测试；`disassembly.txt` 可把地址对应回汇编/源码。`elf-info.txt` 核对 CPU/大小端/浮点属性；`build.log` 查看编译错误；`build-command.json` 是实际参数数组，不要把它直接当 shell 字符串执行。

新增一例通常只需在 `cases.c` 加一个 `uint32_t test_组名_名称(void)`，设置输入、调用真实公开函数、用 `CHECK`/`NEAR` 断言后返回0。示例：

```c
uint32_t test_calibration_disabled_example(void)
{
    sim_config.cal_enabled = 0;
    CHECK(NEAR(cal_correct_flow(5.0f), 5.0f));
    return 0;
}
```

同一组会自动发现该函数；若加入不同生产入口，在 `required_target()` 声明目标，并补充来源检查。新增生产模块需登记 `PRODUCTION` 和依赖边界；测试 C 文件不加入 IAR/CubeIDE 正式工程。不要修改生产文件来导出 static 算法，优先从公开接口/真实接收路径触发。

错误处理：依赖缺失使用上述 venv 解释器重新安装；编译器找不到就明确传 `--cc`；缺 objdump/readelf 则安装完整 Arm GNU 工具链；链接 undefined reference 表示尚未提供某个硬件边界，先确认它是否属于待测逻辑；非法地址先确认缺模型还是生产越界，不能自动映射整个地址空间使测试通过。若 DLL 加载被安全沙箱阻止，应在获准的本地终端运行。本运行器只给编译器子进程设置项目内临时目录，不更改全局 TEMP。

## 8. 当前边界与待上板事项

这是重新编译的真实模块，**不是 IAR 发布固件二进制，也不是整板启动**。与正式工程差异：GCC软件浮点库而非IAR库；`-fno-builtin`；测试链接布局省略向量表、固件头、Bootloader、邮箱与启动初始化；参数存储替换为只读输入配置；只保留被测试引用的路径。CubeIDE正式工程的启动/HAL/C库调用组合仍未完整运行。

没有覆盖：`main.c` 的完整信号链、DAC（数模转换）/PWM（脉宽调制）输出保护、FC03/05/06/10及`Uart2_Communication`分发、UART DMA/IDLE接收中断、Flash保存失败/掉电回滚/旧版参数迁移、Bootloader升级、NVIC优先级和抢占、看门狗与复位。没有依据宣称这些路径通过；未涉及的功能不靠占位替身伪造“通过”。本组也未验证16位采样计数器在主循环长时间停顿后跨越回绕；正常测试以短间隔显式调度。输入过期后的产品安全策略及标志应何时立即恢复仍需另行确认，当前只检查已有的“采样点更新 ModuleState”行为。

仍需正式 IAR/CubeIDE 构建、真实屏幕/按键、传感器电气链路、RS-485（差分串行总线）方向切换与冲突、4~20mA及功率回路、Flash掉电、真实中断时序、看门狗复位时间和整板测试。指令模拟通过只说明**本次输入、边界模型和断言覆盖内的软件行为**。
