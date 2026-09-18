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

### S9 启动状态机 + 跳转 + G1-d 分区修订（commit 124978a + 修复系列）

- 日期：2026-09-17
- 变更：`bl_jump.h/c`（§4.4 固化模板）；`main.c` 完整启动状态机（§4.3）；`bl_flash` 新增 `set_app_window`（备份失败 ⇒ App 区擦写全拒）；PA0 恢复模式（≥3s+释放+10s 二次确认，未按零延迟；进入时固定 115200 重配串口）；LED PB5。
- **G1-d 分区修订（决策 D7）**：6KB 分区首次全量编译实测 7,480B 触发方案级重评 ⇒ BL 页 0~7（8KB）、备份页 8、App 基址 0x08002400（52KB，余量 7,080B）。方案 §3 修订块+正文+附录同步，v3.0~v3.2 变更日志保留历史值。
- 编译：0 错 0 警；修复后实测 **7,548 B / 8,192 B（余量 644 B）**。
- 审查：首轮 **FAIL**（7 Major + 8 Minor）→ 全部处置 → 复审 **PASS**（同一位独立 agent，2026-09-17）。7 项 Major 修复经行为推演确认（层 3 误标洗白路径消除、G3 随新镜像重算、三处 G1-d 同步、WRP 论据重评、契约统一）；复审遗留 3 条 Minor（方案 §9 两处 Page 6 残留、三处代码注释 Page 6、pre_jump 哨兵角落）已在 S10 收尾 commit 清掉；复审建议采纳：方案 §4.7 优先级 5 措辞改"固定 115200 守候"（XModem-CRC 接收方必须周期发 'C'，"仅监听"字面不可实现）。
  - **Major-1（代码）** 回跳路径 `pre_jump(…, verified=1, …)` 把"本靴从未复算层 3"的镜像误标"已验证"（cmd/G3/PA0 进入升级后窗口耗尽回跳的场景）→ 层 3 拆出 `verified_this_boot` 出参，回跳路径传真实标志；
  - **Major-2（代码）** pre_jump 无条件 `boot_attempt++` ⇒ 升级成功后新镜像首次崩溃即被旧计数≥3 误锁定（G3 不随新镜像重算）→ pre_jump 在 build_id 变化且 verified 时重置 `boot_attempt=0` 再 +1；
  - **Major-3/4/5（同步）** 方案 §5.1 `VECT_TAB_OFFSET=0x2400`、日志前置清单第 1 条、`bl_flash.h` 白名单注释（旧"Page 6"在新分区下位于 BL 区内部，严重误导）全部改为 G1-d 值；
  - **Major-6（论证）** WRP 不可用论据在 G1-d 分区下失效（页 0~7 恰可精确保护 BL 不连带）→ §10.3 重写为"不采用但降级为量产可选（Q5 保留）"，理由改为 OB 写禁令/工序等价/白名单已覆盖；
  - **Major-7（契约）** `bl_proto.h` 前提与 main 备份失败行为矛盾 → 前提改为"调用方须已关闭 App 写窗口"，并声明备份失败现场恢复路径（断电重上电重试 backup_ensure 或 SWD）；
  - Minor：CRC 自检快闪循环分段喂狗（防 100ms 残余预算复位循环）；PA0 恢复固定 115200 重配串口（原用三级链结果，与 §4.7 优先级 5 承诺不符）；bl_common.h 体积口径统一 7,480；方案 §5.2 邮箱 0x18 补 D3 字段名；§4.2 预算行加"G1-d 前"标注；正文残留 Page 6→8 清理（历史变更日志保留）。
  - **Minor-11（记录不处置）**：LED 等待慢闪仅在会话返回间隙采样（15s 窗口期间长亮）——观感与 §4.1"慢闪=等待"有偏差，功能无影响，实机阶段再定是否细化。
- **栈核算归档**（S7 登记项）：map 无 stack usage 章节（ewp 未开 `--stack_usage`，该选项在 ewp schema 下不可靠），以 map 静态事实 + 审查员估算归档：.data 16B + .bss 0x6CB(1,739B) + CSTACK 0x400(1,024B) = 2,779B / 19,456B；最深链 main→proto_session→store_block→bl_flash_program_halfwords 峰值估算 <400B，1KB CSTACK 余量充足。
- 体积轨迹：首测 7,480 → 修复后 7,548 → S10 收尾 7,540 B（余量 652 B）。

### S10 工程收尾（本 commit）

- 日期：2026-09-17
- 变更：复审遗留 Minor 清理（方案 §9 Page 6→8 两处、bl_info.h/legacy_param_read.h/bl_info.c 注释三处、pre_jump 重置条件去掉 last_verified==0 哨兵——重置方向安全）；工程清单核对（11/11 源文件 + startup；Project.eww 双工程）；清除 BL 输出目录早期残留的旧名产物 `UMF.hex`（§10.3 防误烧治理）；最终构建归档。
- 最终构建：**0 错误 0 警告；ROM 7,540 B / 8,192 B（余量 652 B，G1-b 等效带）；RAM 2,779 B / 19,456 B**；产物 `EWARM/UMF_Boot/Exe/UMF_Boot.out` + `UMF_Boot.hex`。
- BL 开发完成判定：S1~S10 全部通过分步独立审查；三方终审见下。

### S8 XModem-CRC 会话（commit 1bdcd67 + 修复 2117610）

- 日期：2026-09-17
- 变更：`bl_proto.h/c`——建立窗口 15s/每 1s 'C'、单包超时 max(1.5s, 2×传输+0.5s)、连续 NAK≤6 超限 CAN×2、页首触即擦（54→52 页位图随 D7 更新）、三重包校验、静态头尽早校验、EOT 收尾全链校验后写头（state 最后）；**D6 元数据包**（UMFM+img_size+crc32）补全方案 §6.2 的带内期望 CRC 缺口。
- 编译：0 错 0 警。
- 审查：首轮 **FAIL**（1 Critical + 2 Major + 3 Minor）→ 修复 → 复审 **PASS**：
  - **Critical** getc 喂狗计时的局部变量导致 50ms 轮询片永远凑不满 100ms 阈值——建立窗口 15s 必被看门狗复位（方案 §4.6 时序完全不可用）→ 喂狗计时改跨调用 static 且检查先于 RXNE 返回（同时消除 2400bps×STX 4.3s 字节流盲区）；
  - **Major** 重复包（ACK 丢失重发）6 次 NAK 耗尽整会话作废 → 补标准 XModem"前一包号+CRC 通过 ⇒ ACK 丢弃"恢复路径（不计 NAK）；
  - Minor：CAN 等待的 SOH/STX 回灌；EOT 单字节无校验与 REJECTED 可观测性按 D4 配套工具语义记录（工具侧约定"EOT-ACK 后 N 秒内 'C' 重现 ⇒ 失败"，登记到上位机实现说明）；头注释契约同步。
  - 复审确认：恢复分支不写盘不递增 expected 不计 NAK——标准解死锁语义；关键不变量（expected_blk 仅在完整处理后递增）成立；误判由整镜像 CRC32+meta.size 双兜底。

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

1. App ICF 迁 `0x08002400` + `USER_VECT_TAB_ADDRESS`/`VECT_TAB_OFFSET=0x2400`（`Core/Src/system_stm32f1xx.c`；D7 G1-d 修订值）；
2. `.fw_header` 32 B const 保留区（`App+0x200`，静态字段按 §4.5 表、BL 写入区全 `0xFF`）+ 后构建断言（§6.2 四项；**断言② 按 D2 口径实现：`u32@bin[0x200] == 0x554D4648`，内存字节序为 "HFMU"，不是 "UMFH" 字节串**）；
3. RAM 邮箱写入/清除 + Modbus 寄存器 40127/40128~129/40130/40131；
4. `param_store` 3 页轮转 + 16 B BL 通信槽（M1，格式与 BL 侧 `bl_info.h` 一致后冻结）；
5. App IWDG 放宽至约 1 s（`IWDG_PRESCALER_64`+624）+ `main()` 开头原始喂狗 `IWDG->KR=0xAAAA`；
6. 上位机 `tools/ufl_update.py`：按 **D1/D4/D6 口径**——①先把 `.bin` 补齐到块大小整数倍再按"跳过 32B 头区"口径算 CRC32 与 img_size；②包 CRC16 用 CCITT-FALSE（init 0xFFFF，非通用 XModem 的 0x0000）；③最后一个数据包后、EOT 前发送 **D6 元数据包**（SOH/128B，包号顺延，"UMFM"+img_size+crc32+0xFF 填充）；④失败判定约定：EOT-ACK 后数秒内 'C' 重现 ⇒ 升级失败（BL 其余失败路径静默回等待窗口，见方案 §6.1）。

## 验证状态汇总

| 项 | 状态 |
|----|------|
| 分步编译（iarbuild） | S1 起 0 错误 0 警告（终态 7,540B ROM / 2,779B RAM） |
| 分步独立 agent 审查 | S1~S10 全部通过（S3/S4/S5/S6/S8/S9 经 FAIL→修复→复审循环） |
| 三方终审（2026-09-17） | **全部通过**——R1 内存布局/跳转/启动（PASS，实跑编译复核一致）；R2 存储/备份/掉电安全（PASS，白名单做源码+二进制双重验证、备份链/FMEA 逐行确认）；R3 协议/交付完备性（首轮 FAIL：D6 未回写方案 §6 + 前置清单缺口 → commit 69cc45f 修复 → 复审 PASS，协议文档-代码逐字段一致）。遗留均为记录性 Minor（已随 69cc45f/最终收尾 commit 清理） |
| 实机验证（T-01~T-43） | **冒烟测试通过（2026-09-18，见下节）**；升级全流程/G3/40127 触发链已随 A6/H6 实机闭环（k1→触发→k2 52 块零重传，见 A6 节）。**剩余未实测**：掉电注入、T-07 同包重发、9600 波特、多从站总线（见"剩余验证项"） |

## 实机冒烟测试记录（2026-09-18）

测试环境：J-Link V9.76（SWD）+ USB 转 RS-485（COM6），测试机为可牺牲设备。

| 项 | 方法 | 结果 |
|----|------|------|
| 回滚保护 | 烧录前 `savebin` 全片 64KB → `D:\bl_test\rollback_full.bin` | ✓ 已存档 |
| 烧录范围 | `loadfile UMF_Boot.hex` | ✓ 仅页 0~7（日志"1 range affected (8192 bytes)"），参数页 54~63 未动（dump 0xD800 处原数据完好） |
| 启动/CRC 自检 | 复位后观察串口 | ✓（自检失败会停滞不发 'C'） |
| 固件头判定 | 首次启动行为 | ✓ 判无效 → 升级模式 |
| 参数自动备份 | SWD 回读页 8 → 结构解析 + **CRC32 独立复算（node，含向量自检）** | ✓ magic BLK1/ver=1/n=10/10 组描述符与 payload 精确对应/页 61 含七点标定默认表 0/3/10/25/50/75/100/页 58 含 addr=2+115200 8N1；**legacy_fmt_id=1——页 54 双代回退在真机命中**；calc=stored=0x15ECB00E |
| 通信参数三级链 | 串口波特 | ✓ 旧格式页无 PPG1 页头 → 槽读取落空 → 缺省 115200 8N1 |
| RS-485 守候 | COM6 监听 4s | ✓ 收 5 个 'C'（每秒 1 个） |
| 噪声容忍 | 注入 AA 55 00 FF 4 字节 | ✓ 静默忽略（无 NAK/CAN），'C' 节奏不变（§4.6 总线静默前提下的防御行为） |

遗留可观察项（需人工）：PB5 LED 指示（极性假设低=亮，实测可调）、PA0 恢复模式（长按 ≥3s 松开再按一次）。

## 终审后记（最终状态）

- 分支 `feat/bootloader`，HEAD 通过 S1~S10 + 终审全部审查环节；
- BL 交付边界：仅 BL 侧（Boot/ 11 模块 + UMF_Boot 工程）；App 侧前置改造与上位机工具见"App 侧前置改造清单"（其中第 1 条地址/偏移为 D7 修订值，第 6 条为 D1/D4/D6 完整口径）；
- 端到端可用性：BL 已可独立部署（SWD 烧页 0~7 → RS-485 升级 App）；App 接入需完成前置清单 1~5。

## App 侧前置改造（A 系列，2026-09-18 起）

上位机实机全链路升级验证通过后启动。要求与 BL 开发一致：每步独立 agent 审查通过才继续，逐步记录于本节。
硬件危险期提示（A1 审查登记）：param_store 迁移（A3/A4）落地前，App 镜像严禁在实机执行任何 Flash 参数写入路径（菜单保存 / DAC 校准 / Span），否则将擦除自身代码区（新 App 区覆盖旧参数页 54~60）。

### A1 — App 链接迁移 + 后构建镜像（2026-09-18，审查 PASS 7/7）

改动：
- `EWARM/stm32f103xb_flash.icf`：intvec 0x08002400、ROM [0x08002400,0x0800F3FF]、RAM [0x20000000,0x20004BFF]（避开 0x20004C00 起 BL 邮箱 512B）、新增 `place at address 0x08002600 { readonly section .fw_header }`；
- `Core/Src/system_stm32f1xx.c`：启用 `USER_VECT_TAB_ADDRESS`，`VECT_TAB_OFFSET=0x00002400U`（Cortex-M3 VTOR 128B 对齐满足）；
- `EWARM/UMF.ewp`：BUILDACTION post-build `ielftool --bin --fill 0xFF;0x08002400-0x0800F3FF` → `EWARM/UMF_app.bin`（UMF.hex 原样保留）。

构建实测：0 错 0 警；`.intvec`@0x08002400 大小 0xEC（<0x200，与固件头无重叠）；ROM 实占 47,044B（区余量 ~6.2KB）；CSTACK$$Limit=0x20001588（≤BL_RAM_LIMIT 且 8 对齐）；UMF_app.bin 恒 53,248B（fill 到区上界），SP=0x20001588 / PC=0x0800D781(Thumb) 均在 BL/host 跳转校验范围内。
审查（agent_473ac3ee）：7/7 PASS，ICF 常量与 bl_common.h 逐项一致；Minor①（ICF 邮箱注释 1KB/512B 措辞）已当场修正并复编；Minor② = 上述硬件危险期提示。bin[0x200..0x21F] 暂为代码占位属预期，A2 段保留后解决。

### A2 — .fw_header 固件头常量 + boot_flag 邮箱模块（2026-09-18，审查 PASS 7/7，经 1 轮 FAIL→修复→复审）

新增文件：
- `BSP/boot_flag.h` / `BSP/boot_flag.c`：`.fw_header` 32B 常量（`#pragma location` + `__root`，ICF 固定放 0x08002600；静态字段 magic/hw_id=0x0103/bl_min=1/app_ver/build_id/fmt=1，rsvd0 与 BL 写入区全 0xFF）；`boot_mailbox_t` 32B（布局=bl_info.h 逐字段）+ 读（magic+反码+CRC32 三重校验）/clear（cmd=NONE、boot_attempt=0、seq+1）/request_upgrade（cmd=UPGRADE，保留有效字段）；`boot_crc32`（ISO-HDLC，与 BL/host 三方同口径）；`boot_flag_init`（CSTACK 上界断言 + 启动期邮箱快照）+ `boot_flag_iap_mirror`（40130 回显源）；负数组布局断言 7 条。
- `BSP/app_fw_version.h`：APP_FW_VERSION 0x00010700 (v1.7.0) / APP_BUILD_ID 0x20260918（A6 双版本构建只改此文件）。
- `EWARM/UMF.ewp`：BSP 组加入 boot_flag.c。

构建实测：0 错 0 警；map `.fw_header const 0x8002600 0x20`；bin 全部四项 §6.2 断言过（magic@0x200=0x554D4648、0x211..0x21F 全 0xFF、SP/PC 合法、53,248B）。
审查（agent_886bb6cb）：首轮 FAIL —— **boot_flag_init 的 `(uint32_t)CSTACK$$Limit` 缺 `&`**（读的是无存储绝对符号地址处的随机 SRAM，断言失效且有误触发死循环风险）；修复为 `(uint32_t)&CSTACK$$Limit`（IAR 绝对符号标准取法）后复审 PASS。非阻塞备忘：①上位机 FirmwareImage.cs 实际不查 0x211..0x213（rsvd0），文档表述以代码为准；②A4/A5 接入 main() 后建议做一次"改坏 ICF 触发断言"负向实机验证（T 系列）。
另：A2 期间误删 `BSP/Src/cal_table.c`（BSP 为混合平铺布局的历史特例），已即时从 git 恢复，无损失。

### A3 — param_storage 3 页轮转重写（2026-09-18，审查 PASS 10/10 + 复核通过）

改动：
- `BSP/param_storage.c` 全量重写：Page 61~63（0x0800F400~0x0800FFFF）3 页轮转整页镜像存储，页格式/BL 槽逐字段对齐 bl_info.c 冻结契约（页头 PPG1/seq/state/fmt/data_crc16(覆盖 0x010~0x10F 含槽)/hdr_crc16(覆盖 0x000~0x009)；槽 BLCF@0x100/uart_config@0x104/slot_crc16@0x108）；读取取有效页 seq 最大（int16 回绕差值）；提交目标按 §7.2 v3.1 四规则（优先无效页→全有效选 seq 最小→并列物理序号→new_seq=max+1）；`store_page_guard` 只放行 3 页基址（T-35）；提交期喂狗（擦前后+每 64B）；回读逐半字+整页有效性校验。旧 eeprom.c 依赖全部移除。
- `BSP/param_storage.h`：param_basic_t 新增 dac_zero/dac_full；新增 API param_get_dac_zero/full、param_set_dac_values（一次提交）、param_set_span_values（一次提交）、param_get_status 与 PARAM_STATUS_MIGRATED/PARTIAL/DEFAULTS；param_set_value_4ma/20ma 维持 RAM-only（与旧版一致），落盘分工给 set_span_values。
- NaN/±Inf 解码防护（审查建议当场落实）：`dec_f()` 对指数全 1 位形（含 0xFFFFFFFF 擦除态）回落逐字段默认值，decode 全部 17 个 float 字段走该辅助。

构建实测：0 错 0 警；ROM 47,478B / RAM 5,787B（+s_page_img 272B static）。
审查（agent_3914a5d2）：10/10 PASS——页格式与 BL 解析逐字节闭合（字节序/CRC 覆盖范围/uart_config 钳制三点确认 BL 读 App 页全过校验）、CRC16 与 bl_crc 位级等价、三场景提交目标推演正确、掉电至多损坏目标页；NaN 防护补丁复核通过。事实更正：eeprom.o 此刻仍是活代码（main/bsp_menu/bsp_usart 的旧调用未删，map 占 522B）。
**部署闸门（A4 未完成前）**：A3 固件严禁刷入带旧标定数据的设备——三页均无效时以默认值提交会擦掉旧 Page 61 标定数据且 Page8 备份不存在的设备不可恢复；旧 span 写 Page 63 与新存储页 3 同页双写者待 A4 移除。

### A4 — 旧数据迁移 + 旧页调用点改造 + Data_Init 重写（2026-09-18，审查 PASS 10/10 + 复核通过）

改动：
- `BSP/param_storage.c`：无有效新页分支接入 `migrate_from_legacy()`——数据源优先级：旧页 61~63 直读（`legacy_read_group`，槽扫描语义与 eeprom.c/BL legacy_param_read.c 逐条一致，双代回退返回实际 len）> 页 8 备份块（`backup_blob_read` 只读，magic/ver/n/crc32 四重校验；**desc[1]=found?len:0**，desc[3] 为 rsvd——开发者首版误当 found 标志，经真机 dump 仿真发现后修正）> 默认值兜底；54~60 一律走备份块（已落入 App 代码区，直扫代码字节有误匹配风险）；`legacy_apply` 10 组逐字段解码（0xFFFFFFFF/NaN/Inf 缺失跳过，clamp 保留），PARAM_STATUS 三态（0x3FF=MIGRATED / 非零=PARTIAL|DEFAULTS / 零=DEFAULTS）；部分迁移不自动改写 modbus_addr/uart_config。
- `Core/Src/main.c`：启动顺序 `param_storage_init()` → `Data_Init()`（§5.4 硬性要求①）；`Data_Init()` 重写为 store→RAM 同步（param_get_dac_zero/full + param_get_value_4ma/20ma），保留 Span/DAC 自愈；删除旧反向同步。
- `Core/Inc/main.h`：删除 `DAC_FLASH_PAGE_ADDR`（注释禁恢复）与 `#include "eeprom.h"`。
- `BSP/bsp_menu.c`：SCR_DAC_ZERO/FULL → `param_set_dac_values`；SCR_SPAN_ZERO/FULL → `param_set_span_values`；SCR_FACTORY_RST → reset 后 param_get 同步 RAM。
- `BSP/bsp_usart.c`：FC10 DAC/Span → 统一存储 setter；删除 BackupBuf。
- `EWARM/UMF.ewp`：移除 eeprom.c（全仓零调用者，文件保留）。

真机数据仿真（`D:\bl_test\sim_migration.js`，rollback_full.bin + page8_backup.bin 双 dump 复算）：直读命中 61（meter=0.001+标定表）/62（0.100）；备份块命中 54(len=1 旧代回退,fmt_id=1 真机命中)/55/58(addr=2,115200 8N1)/60/61/62；56/57/59/63 真机为空→默认；found_mask=0x1D3→PARTIAL|DEFAULTS。与 BL 冒烟测试对页 8 的独立解析一致。
审查（agent_b6c7cc04）：10/10 PASS（扫描数学/双代回退/备份块解析/逐组等价/优先级/§5.4 三条/掉电幂等/状态语义/调用点完整性/栈安全）；范围外发现 **A3 遗留缺陷：新页直载路径 memset 把 pwd_engineer 清 0（鉴权绕过）**——已改为 `param_load_defaults()` 预置（34 字段覆盖矩阵复核通过）。备注：40131 为组级粒度（字段级缺失不单独置 bit2，登记为已知限制）。

### A5 — Modbus IAP 寄存器 + 邮箱触发 + IWDG 放宽 + 启动顺序收尾（2026-09-18，审查 PASS 10/10 + 复核通过）

改动：
- `BSP/bsp_usart.h/.c`：新增 PDU 126~130（4x 40127~40131）——FC06 写 126=0x5AA5 置 `s_iap_reset_pending`（延迟复位：`bsp_usart2_check_baud_rate_pending()` 开头处理，等 DMA+TC 完成（gState READY）→ `boot_mailbox_request_upgrade(param_get_uart_config())` → `NVIC_SystemReset()`）；FC03 新增 126~130 读块（127/128=固件头 app_ver 低/高字、129=启动期邮箱 cmd 回显、130=PARAM_STATUS、126 读回 0）。
- `Core/Src/main.c`：USER CODE 1 开头 `boot_flag_init()`（§5.4 第一步：CSTACK 断言+邮箱快照）+ IWDG 喂狗/放宽（0xAAAA→0x5555→PR=64→RLR=624→0xAAAA，1.0s）；USER CODE 2 末尾（MX_IWDG_Init 后）`boot_mailbox_clear()`（迁移确认后清 cmd+G3）。
- `Core/Src/iwdg.c`：USER CODE IWDG_Init 2 覆盖生成代码的 100ms 为 1s（同参数；HAL_IWDG_Init 自带 PVU/RVU 同步等待，覆盖写有效）。
- `Core/Inc/main.h`：删除 `#include "eeprom.h"`。

构建实测：0 错 0 警；bin 53,248B 四项断言过（SP=0x200017C0 / PC=0x0800E011）。
审查（agent_df590375）：10/10 PASS——触发链端到端推演闭合（TC 后复位、邮箱写序、BL 侧校验/优先级 1 采信）、跨块读取与既有行为一致、IWDG 键序列与 PVU/RVU 时序安全（1s 窗口下最坏路径余量充足）、上位机 TriggerDeviceBootloaderAsync 兼容（复位<100ms + 30s 'C' 窗口）。Minor 已当场修复：**request_upgrade 有效分支同时刷新 mb.uart_config**（同会话改波特率后触发升级的失配消除），复核通过。
已知限制登记：FC03 跨块/越界读取回发缓冲残留字节为既有全局行为（非本次引入），上位机全量快照应按块内地址读取。

### A6 — 流量系数双版本测试构建（2026-09-18，代码审查 PASS 7/7；实机验证待 485 适配器恢复）

改动（均为代码与产物，仓库提交状态为正式版配方）：
- `BSP/app_fw_version.h`：新增 `APP_FORCE_METER_COEFF`（0.0f=正式版不覆盖；k1=1.0f+ver 0x00010700+build 0x2026A001，k2=2.0f+ver 0x00010701+build 0x2026A002，配方写入注释）。
- `BSP/param_storage.c`：`param_storage_init` 末尾（两分支汇合后）仅 RAM 覆盖流量系数（不落盘；0.0f 时编译期消除——k1↔k2 bin 仅 4 字节差（ver/build/浮点字面量）、正式版↔k1 全局收缩佐证）。
- 测试产物：`D:\bl_test\UMF_app_1.7.0_k1.bin`、`UMF_app_1.7.1_k2.bin`（各 53,248B=52 块 @1024，magic 校验过；"46 块"旧口径更正为 **52 块**，@115200 全程约 12~15s）。
- 联调探针 `D:\bl_test\h5probe\Program.cs`：subcommand 化（upgrade/mread/mwrite/listen；自带 Modbus CRC16，mread float 字节序经 0.001=0x3A83126F 推演验证：[24]=0x126F、[25]=0x3A83）。

审查（agent_4a172178）：7/7 PASS——覆盖时序（commit 先于覆盖）、正式版常量折叠无残留（bin 字节级证据链）、三方可分辨闭环（文件名/版本/系数）、探针 CRC 与字节序正确、init 重构无回归（pwd 修复完好）。非阻塞备注：①h5probe 引用仓库外上位机工程，上位机升级链路（FirmwareImage/XModemSender/SerialPortChannel/XModemCrc）尚未同步回仓库内 HostApplication——**待办**；②A1~A6 改动随本条提交。
**实机验证完成（2026-09-18，探针 h5probe，COM6@115200）**：
- k1 App 存活读数：PDU 24/25 float=1.0、[127/128]=0x0700/0x0001（v1.7.0）、[129]=0（镜像 0）、[130]=0x01（迁移后新格式直载）；
- 触发链：`mwrite 126 0x5AA5` → FC06 回显 OK → 设备复位进 BL → `listen` 听到 'C'×6（1Hz）——**A5 触发链实机 PASS**；
- 触发后立即升级 k2（`upgrade` 探针）：**52/52 块零重传 10,804ms 成功**；
- k2 读数：PDU 24/25 float=2.0、[127/128]=0x0701/0x0001（v1.7.1）、[129]=0x01（本次启动确系升级请求进入，§5.3 设计吻合）、[130]=0x01（k1 提交的新格式页直载，无二次迁移）。

**用户实测缺陷与根因（H6，已修复并闭环）**：用户第一次升级（→k1）成功、第二次（k1→k2）"等待 'C' 超时"。逐环实测证实设备侧触发链正常；根因是 **BL 'C' 建立窗口 15s 的冻结行为（窗口耗尽且 App 有效 ⇒ 回跳 App）+ 上位机"触发/开始升级"两按钮分离**，人工间隔 >15s 必超时（首次成功仅因设备处于 G3 锁定 'C' 永续）。修复在上位机侧（开始升级一键原子触发，commit 112d232，独立审查 PASS），BL 冻结行为不动。
通过标准达成：k1/k2 双版本烧录读值区分（系数 1.0↔2.0、版本 1.7.0↔1.7.1）+ 40127 触发 + 参数迁移全部实机闭环。

#### A6 实机验证操作单（已执行完毕，留存供复测参考）

1. 探针 `D:\bl_test\h5probe`：`dotnet run -- listen <COMx> 5000`（听 'C'）→ `mread <COMx> 24 2`（流量系数）→ `mwrite <COMx> 126 0x5AA5`（触发）→ `upgrade <COMx> <bin>`（升级）。
2. 上位机 UI 复测：连接设备 → 固件升级页选 bin → 点【开始升级】（已内置自动触发，注意 BL 'C' 窗口 15s——触发后需在窗口内开始传输，一键流程自动满足）。
3. 设备已在 BL 等待态时可免连接直接开始升级（方式 A）。
通过标准：k1 读值系数=1/版本 1.7.0，k2 读值系数=2/版本 1.7.1，40130 镜像=1（升级请求启动），40131=0x01。

## A7 三方终审（2026-09-18）

| 审查员 | 专责 | 首轮 | 复审 |
|--------|------|------|------|
| R1（agent_8f308a8e） | App 侧固件全量一致性（冻结契约/启动时序/迁移/触发链/实机预言/遗留物/构建产物 7 项） | **PASS** | — |
| R2（agent_771388a1） | 上位机 H6 修复终态 + 双入口矩阵 + 文档一致性 | FAIL：接口注释"将来 App 部署后使用"残留、已知限制表 #2"46 块未测"过期 | **复审 PASS**（db04d1a，残留扫描零命中） |
| R3（agent_3763ca3d） | 交付完备性与文档真实性（日志真实性/资产/复现路径/限制真实性/纪律/版本号 6 项） | FAIL：commits 12→13、setup.iss 待办 V1.6.0→V1.6.1、限制表 #2 同上、BL 汇总表"升级全流程未开始"、断链引用《首次现场切换规程》 | **复审 PASS**（22f82c6+db04d1a，6 项全闭环） |

**A7 终审结论：三方全部 PASS。** Bootloader 升级全项目（BL + 上位机 485 烧录 + App 侧前置改造 A1~A6 + H6 竞态修复）闭环，可交付合并。

修复（均文档级，无代码改动）：上位机接口注释/触发失败文案/限制表 #2/commits 数/setup.iss 待办/部署前提引用；BL 日志验证汇总表；两个历史设计文档（BOOTLOADER_PLAN_COMPARISON.md、DESIGN_bootloader_upgrade.md——后者为已废弃方案 B，仅存档）随本提交入库归档。
R1 非阻塞建议登记（不改代码）：①main.c/iwdg.c 的 PR/RLR 直写补 PVU/RVU 等待（对齐 bl_iwdg.c 实践，实机全程无异常佐证当前可用）；②BSP/eeprom.c/.h 已无调用者，归档或删除；③HostApplication/ 副本与上位机真仓不同步（升级链路 4 文件待同步，README 待加指针）。

## 剩余验证项（已知限制，两仓已如实登记）

1. 9600 波特率实机升级（当前仅 115200 实测）；
2. 传输中掉电/断连注入（单测覆盖四类注入，真机未做）；
3. 多从站/多主站总线共存；
4. 上位机 UI 一键流程人工走查（探针已覆盖全部后端路径）；
5. 正式发布构建镜像（产品 App）随发版一并实测（H6 的 52 块满区镜像已超集覆盖长传输）；
6. 发版流程：setup.iss 版本号同步 V1.6.1；两分支未推送/未合并（用户未要求）。
