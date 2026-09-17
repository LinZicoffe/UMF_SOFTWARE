# BL（Bootloader）开发更新日志

- **方案依据**: [UMF_Bootloader_Upgrade_Plan.md](UMF_Bootloader_Upgrade_Plan.md)（v3.2，已过三方独立审查）
- **开发分支**: `feat/bootloader`（自 `master` 切出）
- **工作流**: 每完成一小步 → `iarbuild` 真实编译 → 独立 agent 审查 → 通过后才继续；本文档随每步更新
- **编译验证**: IAR 命令行 `C:\IAR\ewarm-960.4\common\bin\iarbuild.exe UMF_Boot.ewp -build UMF_Boot`（EWARM 9.60.4，与 App 工程同版本）
- **参考资料**: [OpenBLT](https://github.com/feaser/openblt) —— 仅作实现对照参考（F1 Flash 错误处理 / RS-485 DE 时序 / backdoor 超时 / 看门狗喂点四处核对），**不引入其任何代码**（GPLv3，与本项目闭源固件不兼容）

## 关键实现决策（评审后冻结，改动需重新评审）

| 编号 | 决策 | 依据 |
|------|------|------|
| D1 | **img_size 口径**：固件头 `img_size` ≡ XModem 实际接收并写入的总字节数（末包按块大小对齐、填充 `0xFF` 计入）；上位机必须先把 `.bin` 补齐到块大小整数倍再计算 CRC32 与元数据 | 方案 v3.2 未定义 XModem 的文件长度传递方式（终审前评审发现的缺口）；本约定使三条 CRC 路径（主机算 .bin / BL 算 Flash / 启动期层 3）零歧义 |
| D2 | **魔数字节序**：所有魔数（"UMFH"/"PPG1"/"BLCF"/"BLOT"/"BLK1"）统一按 u32 小端数值比较，不做字节串比较；App 侧固件头初始化数组内存字节序为 `"HFMU"`（即 `0x48,0x46,0x4D,0x55`） | 方案 §4.5 的 C 初始化数组与构建断言文本存在字节序矛盾（同源评审发现），以 u32 比较统一消歧 |
| D3 | **层 3 跳过依据**：RAM 邮箱 `0x18` 偏移复用为 `last_verified_build_id`（u32，落在 CRC32 覆盖范围 `0x00~0x1B` 内） | 方案层 3"暖复位且 build_id 未变则跳过"缺少存储位置（评审发现的缺口） |
| D4 | **包 CRC16 = CCITT-FALSE**（poly 0x1021 / init 0xFFFF / 无反射 / 无异或，`"123456789"`→`0x29B1`）。与通用 XModem 工具（init 0x0000）**不兼容**，必须使用配套上位机 | 方案 §6.2 定版；两条 CRC16 侧（BL/上位机）必须以测试向量互验 |
| D5 | **BL 工程零 HAL、零浮点**：不链接任何 HAL/CMSIS-system 源文件；C 源不出现 float 运算（浮点常量仅以 u32 位形参与区间比较，见 S6） | 方案 §4.1 编译约束（软浮点库 +2~3KB 会击穿 6,144B G1 门限） |
| D6 | **元数据包约定**：最后一个数据包之后、EOT 之前，上位机发送一个额外 SOH/128B 包（包号顺延），载荷 `[0..3]="UMFM"`、`[4..7]=img_size`（u32 LE，D1 填充口径）、`[8..11]=crc32`（u32 LE，跳过 32B 头区口径）、`[12..127]` 全 `0xFF`；BL 校验包 CRC16 后 ACK（不写 Flash）并存为期望值，EOT 后复算整镜像 CRC32 与之比对；无元数据包的 EOT ⇒ `BL_PROTO_REJECTED` | 方案 §6.2/§6.3 要求 BL 端"整镜像 CRC32 失败⇒不写头"，但 XModem 无带内传递期望 CRC 的通道（实现时发现的方案缺口）；本约定同时使 D1 的 img_size 可带内交叉核对，且天然拒绝不配套的通用 XModem 工具（呼应 D4） |
| D7 | **G1-d 分区修订**：BL 首次全量编译实测 7,480 B（Size 优化，`--mfc` 跨模块编译因 ewp 选项 schema 不可用而放弃；代码级裁剪上限约 640 B 仍超 6,144 B，再裁将触及校验/G3/恢复等安全功能）⇒ 按方案 §4.10 G1-d 门限执行方案级重评：BL 扩为页 0~7（8,192 B，余量 712 B），备份页 6→8（0x08002000），App 基址 0x08001C00→0x08002400（页 9~60，53,248 B，余量 7,080 B 仍满足"≥4KB"验收门禁），固件头 0x08002600，`VECT_TAB_OFFSET=0x2400`；方案文档 §3 已加修订块并同步正文，v3.0~v3.2 变更日志保留历史数值 | 方案 §4.10 G1-d 的既定路径（"裁剪后仍 >6.0KB ⇒ 方案级重评"）；实测值替换了 §4.2 的 4.7~6.0KB 估算——低估主因是旧参数抽取/校验/备份链（S6+S7 合计约 3KB）远超方案预算行（0.4+0.2KB） |

## 步骤记录

> 每步格式：日期 / 变更清单 / 编译结果 / 审查结论（agent 判定 + 问题处置）

### S7 信息层（commit c089f6c + 建议 b57c10f）

- 日期：2026-09-17
- 变更：`bl_info.h/c`——固件头层 1（§4.5 顺序，state 最后判定）/层 2（向量表）/层 3 辅助（两段式镜像 CRC32，跳过 32B 头区）；`bl_info_hdr_write_bl_fields`（img_size→crc32→hdr_crc16→state 最后写 + 回读整体校验）；备份编排（前置条件链、窗口纪律、双重回读复验）；BL 通信槽（页头有效且 seq 最大，槽 CRC+语义校验，坏页顺延）；RAM 邮箱（§5.2+D3，读校验三要素，pre_jump 两种写序）。
- 编译：0 错误 0 警告。
- 审查：**PASS**（1 Major 建议 + 4 Minor，均立即处置）：
  - **Major 建议**：备份调用链栈峰值 ~1KB 逼近 1KB 栈预算 → 三个大缓冲提升为文件级 static（BL RAM 余量 ~17KB）；
  - Minor：`bl_info_image_crc32` 入参界自检（0xFFFFFFFF 无效哨兵）；EMPTY_SRC 注释精确化（"无可抽取的有效记录"，含不可识别）；mailbox_read/slot 补 NULL 防护；删除多余 stm32f103xb.h 包含。
  - 审查确认：层 1 顺序/0xFFA5 半字构造/写序掉电安全（含 crc32 某半字恰为 0xFFFF 的角落由末尾回读闭合）；层 3 边界（0x220 空段、末读不越区）；备份窗口无旁路出口；槽重选循环 ≤3 轮终止；邮箱两种写序的中间态全部"读校验失败=视为无邮箱"安全方向。
- S9 注意（审查要求）：接线后把 IAR map 的 stack usage 归档到日志。

### S6 旧参数抽取器（commit db7a608 + 修复 b763a4d/后续小修）

- 日期：2026-09-17
- 变更：`legacy_param_read.h/c`——旧 10 页（54~63）只读抽取（扫描语义与 eeprom.c 逐条对应，12 种页/Len 组合末槽边界验算通过）；页 54/61 双代长度回退；零浮点区间校验（IEEE-754 位形全序键 + NaN/Inf 拒绝 + -0.0 归一化）；244 B 备份块构建（§7.3 布局，crc32 覆盖 0~239）。
- 编译：0 错误 0 警告。
- 审查：首轮 **FAIL**（2 Major + 4 Minor）→ 修复 → 复审 **PASS**（同一位独立 agent）：
  - **Major-1**：`memset 0xFF` 后未清 `found`——未找到组 desc.len 误写新代长度 → 循环内显式清零；
  - **Major-2（重要设计教训）**：校验过严会把"App 正常自愈运行"的设备挡在升级门外（备份未就绪 ⇒ 拒绝一切擦除 ⇒ **永久无法升级且 BL 无本地提示**）。DAC 零满度组合等自愈类检查全部移除，严格限定方案 §7.3 强制清单（cal_k/cal_pct/modbus_addr/baud idx/sample_interval/filter_window/15 个 float 的 NaN+MIN/MAX）；自愈类字段以原始值入备份，App 恢复走同款自愈逻辑。校准原则已写入函数头注释；
  - Minor：-0.0f 键序归一化（fp_in_range 与 cal_pct 单调路径两处）；页 54 单条旧代记录被 Len=2 视角同步误读的元数据修正（数据无歧义）；旧页基址注释与 eeprom.h 互指。
  - 复审确认：保留项与 §7.3 强制清单一一对应无遗漏无添加；三种页 54 数据来源与 App 字段判据全部对齐；无回归。
- 本步教训登记（供后续模块）：**"宁可不升级"策略的边界**——备份校验只拦"自洽但错误"的抽取结果（长度猜错/格式漂移），不拦 App 能自愈的退化字段值。

### S5 USART2 驱动 + RS-485 DE（commit 72bf03a + 修复 0f28f10）

- 日期：2026-09-17
- 变更：`bl_usart.h/c`——轮询收发 + PA1 DE 半双工时序（发送前拉高、TC 后拉低并保持 1 字符时间）；校验启用时 M=1（8 数据 + 1 校验，与 App 9B 耦合一致，XModem 字节流不变形）；BRR=round(PCLK1/baud) 实时计算（HSI 回退后仍正确）；getc 等待循环每 100ms 喂狗 + ORE 清除。
- 编译：0 错误 0 警告。
- 审查：首轮 **FAIL**（1 Major）→ 修复 → 复审 **PASS**（同一位独立 agent）：
  - **Major**：初始化 DE 极性写反——`BSRR = 1<<1` 是 BS1 置位（拉高=发送态），init 到首次发送期间 RS-485 驱动器持续占用总线 → 改 `1<<17`（BR1 拉低接收态）；
  - 复审确认：CRL 掩码/BRR 数学（各档误差 <2%，HSI 115200 +0.64%）/CR1 位序/ORE 清除序列/TC 等待与清除/无中断无 DMA 自洽——均逐项通过；PE 不检查由整包 CRC16 兜底已注明头文件。
- 遗留硬件验证项：2400 baud 最坏 4.17ms DE 保持忙等、HSI 回退路径 XModem 握手。

### S4 平台层：时钟/时基/看门狗（commit 10879a8 + 修复 7abd736）

- 日期：2026-09-17
- 变更：`bl_clock.h/c`（HSE×9=72MHz，HSE/PLL/切换任一失败回退 HSI；跳转前 deinit 回 HSI）；`bl_time.h/c`（DWT CYCCNT 时基）；`bl_iwdg.h/c`（入口喂狗 + 会话期 5s 重配）。
- 编译：0 错误 0 警告。
- 审查：首轮 **FAIL**（1 Major 必修 + 1 Major 跨步 + 5 Minor）→ 修复 → 复审 **PASS**（同一位独立 agent）：
  - **#1 Major（必修）**：`FLASH_ACR_LATENCY_2` 是 CMSIS"第 2 位掩码"（0x4=字段值 4，非法 4WS）而非"2 等待周期"（0b010=0x2）；HAL 的 `FLASH_LATENCY_2` 实际映射 `FLASH_ACR_LATENCY_1`（ST 社区确认的命名陷阱）。→ 按值语义改名 `BL_ACR_LATENCY_2WS=FLASH_ACR_LATENCY_1`；
  - **#2 Major（跨步）**：暖复位看门狗预算——HSE 失败路径等待最长约 150ms 超过旧固件 100ms 预算。→ 本步把时钟等待上限压到 100k 次（~100ms，对 HSE 事件 25 倍余量）；**闭环手段是 S9 必做项：main 中 `bl_iwdg_feed()` 必须先于 `bl_clock_init()`**（IWDG 未启动时写入无害）；方案 §4.3 启动流程顺序已同步修正（喂狗行上移并注明理由）；
  - Minor：SW 切换失败路径先 SW=HSI 等 SWS 再关 PLL（覆盖"误判超时"极端情形）；回退/降级真 0WS（清域不置位）；deinit 清总线分频使 s_pclk1_hz 与硬件一致（原 4MHz vs 记录 8MHz）；IWDG PR/RLR 写后补一次同步等待再重载（防装入旧 RLR）；两处注释时基/职责修正（复审 R2）。
  - 复审确认：三条回退路径控制流符合 RM0008；DWT 顺序/回绕语义维持前轮结论。
- **S9 必做项登记**：main() 首行 `bl_iwdg_feed()` 先于 `bl_clock_init()`；随后 `bl_iwdg_start_5s()` 进入会话期前调用（方案 §4.3/§4.6）。

### S3 Flash 驱动（commit 15ac339 + 修复 6f49179）

- 日期：2026-09-17
- 变更：`Boot/Inc/bl_flash.h` + `Boot/Src/bl_flash.c`——F1 寄存器级页擦（PER→AR→STRT）/半字编程（逐半字查状态）/回读比对；§10.3 白名单两段式守卫（App 区常开 + 备份页仅窗口期）；per-halfword 预检查（0xFFFF 跳过 / 已等值跳过支持 T-07 同包重发 / 非擦除态且异值拒绝）。
- 编译：0 错误 0 警告。
- 审查：首轮 **FAIL**（2 Critical + 4 Minor）→ 修复 → 复审 **PASS**（同一位独立 agent，2026-09-17）：
  - **Critical-1** `range_within` 缺 `addr <= end_incl` 上界，`addr > end` 时无符号下溢 ⇒ 白名单可被绕过（审查员实测：参数页 62 整页擦、参数页 61 内写、备份窗口开时擦参数页 61 均会被放行）→ 补三段判据后复验三个绕过用例全部拒绝、合法边界（App 末页/末半字/全长、备份页窗口内）不受损；
  - **Critical-2** 等待上限 100k 次对页擦（t_ERASE max 40ms）不足，72MHz 下必然伪超时，且超时路径在 BSY=1 时写 CR 无效 → 分档 WAIT_PROGRAM=100k / WAIT_ERASE=2,000k（≈111ms@4 周期/循环，2.8 倍余量），超时路径改为失败安全（不触碰 CR、恢复靠会话期看门狗）；
  - Minor：擦除寄存器顺序改 RM0008 文档序（PER→AR→STRT）；每次操作前先确认 BSY=0；BL_OK 契约（"目标不劣于期望值"）写入头文件；read16 单次读入局部。
  - 审查员确认：锁定纪律全路径复查通过（六条错误路径 + 正常路径全部 flash_lock，无裸 return）。
- 遗留硬件验证项（记录，非代码问题）：整页擦除时序实测、连续多页擦除在 5s 看门狗预算内的最坏序列、T-07 同包重发实机。

### S2 CRC 模块（commit 968bc99）

- 日期：2026-09-17
- 变更：`Boot/Inc/bl_crc.h` + `Boot/Src/bl_crc.c`——CRC32（ISO-HDLC，反射按位，流式接口 start/update/result）、CRC16（CCITT-FALSE，按位）、`bl_crc_self_test()`（三个 CRC32 定版向量 + CRC16 向量 + 流式 4+5 分段一致性）；工程文件组已注册。
- 编译：0 错误 0 警告（模块暂无调用者，被 --vfe 消除，ROM 仍 348B；实际体积待 S9 接线后在 map 核实）。
- 顺带（非本步范围）：commit dc46647 修复既有 `OLED/generate_chinese_font.py`——安全钩子（Mimosa commit 前扫描）对 `open(...,'w')` 一律报路径穿越高危并拦截提交，改为 stdout 输出 + shell 重定向的生成器惯例，脚本不再做任何文件写入。
- 审查：**PASS**（独立 agent，2026-09-17）。审查员将仓库源文件在宿主机 gcc（-std=c99 -Wall -Wextra）编译执行，全部定版向量实测命中（含追加的逐字节 9 段拆分与 4+0+5 空段一致性）；D4 与通用 XModem（CRC-16/XMODEM，init 0x0000→0x31C3）不兼容的表述确认准确；零静态状态、可重入；与 BSP 的 Modbus getCRC16（0xA001 反射）无混淆。无问题项。


### S1 工程基线（分支 / 公共常量 / IAR 工程 / 更新日志）

- 日期：2026-09-17
- 变更：
  - 新建分支 `feat/bootloader`；
  - `Boot/Inc/bl_common.h`：分区地址（§3）、固件头/邮箱地址（§4.5/§5.2）、魔数（D2）、状态码、`uart_config` 位域解码（与 `param_storage.h:73-81` 一致）、BL 版本与 hw_id；
  - `Boot/Src/main.c`：占位入口（`SystemInit` 空实现承接启动汇编 `startup_stm32f103xb.s:125` 的引用；完整状态机在 S9 实现）；
  - `EWARM/UMF_Boot.icf`：BL 链接区 ROM `0x08000000~0x080017FF` / RAM `0x20000000~0x20004BFF`（让出邮箱）/ CSTACK 1KB / 无堆；
  - `EWARM/UMF_Boot.ewp`：由 `UMF.ewp` 派生（去 `USE_HAL_DRIVER`、包含路径仅 Boot/Inc + CMSIS、ICF 指向 UMF_Boot.icf、独立 Obj/List 目录）；
  - `EWARM/Project.eww`：注册 `UMF_Boot.ewp`（App 工程不受影响）；
  - 本文档。
- 编译：`iarbuild` **0 错误 0 警告**；基线 ROM 348 B（intvec + 启动 + 空主循环），G1 余量 5,796 B。
- 过程修正：ICF 中文注释被 IAR 以本地代码页解析报"注释未闭合"→ 改纯 ASCII 注释（App 工程的 ICF 同样为 ASCII，保持一致）。
- 审查：**PASS**（独立 agent，2026-09-17）。全部地址/魔数/解码与方案 §3/§4.5/§5.2/附录 A/B 逐字一致；SystemInit 空实现论证成立；D1~D5 自洽。4 条 Minor 已全部处置：
  1. OBJCOPY 输出 `UMF.hex`→`UMF_Boot.hex`（防现场烧录拿错同名产物，§10.3 治理目标）；
  2. BrowseInfoPath 独立为 `UMF_Boot\BrowseInfo`；
  3. TrustZone 惰性输出名 `UMF_Boot_import_lib.o`；
  4. 本文档前置清单第 2 条补注：后构建断言② 必须按 D2 口径实现——`u32@bin[0x200] == 0x554D4648`（内存字节序 "HFMU"），不得照方案 §4.5 字面写 `bin[0x200..0x203]=="UMFH"`（该文本与方案自己的 C 初始化数组矛盾）。
  审查备注（记录不处置）：App 工程 ICF 的 RAM end 仍 0x20004FFF、仍用页 54~63 作 EEPROM——属 App 侧前置改造范围；AGENTS.md 写 EWARM 8.32 与实际 9.60.4 不符（工具链版本信息陈旧，另行更新）。

## App 侧前置改造清单（BL 之外，另行实施后方可端到端联调）

1. App ICF 迁 `0x08001C00` + `USER_VECT_TAB_ADDRESS`/`VECT_TAB_OFFSET=0x1C00`（`Core/Src/system_stm32f1xx.c`）；
2. `.fw_header` 32 B const 保留区（`App+0x200`，静态字段按 §4.5 表、BL 写入区全 `0xFF`）+ 后构建断言（§6.2 四项；**断言② 按 D2 口径实现：`u32@bin[0x200] == 0x554D4648`，内存字节序为 "HFMU"，不是 "UMFH" 字节串**）；
3. RAM 邮箱写入/清除 + Modbus 寄存器 40127/40128~129/40130/40131；
4. `param_store` 3 页轮转 + 16 B BL 通信槽（M1，格式与 BL 侧 `bl_info.h` 一致后冻结）；
5. App IWDG 放宽至约 1 s（`IWDG_PRESCALER_64`+624）+ `main()` 开头原始喂狗 `IWDG->KR=0xAAAA`；
6. 上位机 `tools/ufl_update.py`：按 D1/D4 口径（补齐到块边界算 CRC32、CCITT-FALSE 包校验）。

## 验证状态汇总

| 项 | 状态 |
|----|------|
| 分步编译（iarbuild） | S1 起 0 错误 0 警告 |
| 分步独立 agent 审查 | S1 起（见各步记录） |
| 三方终审（布局/跳转、存储/掉电、协议） | 未开始（S10 后进行） |
| 实机验证（T-01~T-43） | 未开始（需硬件，属方案 M3/M5） |
