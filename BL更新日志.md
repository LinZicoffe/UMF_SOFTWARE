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
- 审查：_待独立 agent 审查后填写_

## App 侧前置改造清单（BL 之外，另行实施后方可端到端联调）

1. App ICF 迁 `0x08001C00` + `USER_VECT_TAB_ADDRESS`/`VECT_TAB_OFFSET=0x1C00`（`Core/Src/system_stm32f1xx.c`）；
2. `.fw_header` 32 B const 保留区（`App+0x200`，静态字段按 §4.5 表、BL 写入区全 `0xFF`）+ 后构建断言（§6.2 四项）；
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
