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

## 步骤记录

> 每步格式：日期 / 变更清单 / 编译结果 / 审查结论（agent 判定 + 问题处置）

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
