# 2026-10-23 接入运行记录

基线：`4d8511b`，初始 `master` 干净；工作分支 `codex/offline-firmware-regression`。没有修改生产源码、正式工程配置，没有提交或推送，没有硬件连接。

## 当前结果

| 检查 | 实际结果 | 退出码/证据目录 |
|---|---|---|
| 最小真实 `itoa_local(INT32_MIN)` | PASS，执行轨迹包含生产入口 | 0；`build/smoke-pass` |
| 临时改为错误预期 -2147483647 | FAIL，明确 C 断言错误 | 1；`build/smoke-wrong-expectation` |
| 恢复正确预期 | PASS | 0；`build/smoke-restored` |
| 正常回归组 | 29 PASS / 0 FAIL | 0；`build/regression` |
| 全部测试 | 29 PASS / 1 FAIL | 1；`build/all`（最终重跑） |
| 没有 Python 依赖（-S隔离 site-packages） | 明确依赖缺失错误 | 2；`build/cli-missing-dependencies` |
| 编译器不存在 | 明确 compiler not found | 2；`build/cli-missing-compiler` |
| 误用电脑 MinGW GCC | 拒绝 `mingw32`，未当成 Arm 模拟 | 2；`build/cli-wrong-architecture` |
| 用例筛选无匹配 | 明确 no matching tests | 2；`build/cli-no-match` |
| 测试文件临时加入 #error | 真实 GCC 编译失败，已恢复 | 2；`build/cli-build-failure` |
| 临时使用非法 CPU 模型号 | Unicorn 初始化 UC_ERR_ARG，已恢复 | 2；`build/cli-init-failure` |
| 原有电脑显示像素回归 | 9项全部通过（不是目标CPU模拟） | 0；`python -m unittest discover -s tests -v` |
| 报告保存错误的退出码测试 | 2项通过；权限错误先复现返回0，修正后返回2 | 0；独立运行 `test_runner_errors.py` |
| 本地wheel恢复后真实执行 | 新建venv离线安装，smoke组2项通过 | 0；`build/recovery-smoke` |

运行器自检在上述29项内：`.data/.bss`初始化、非法读、非法写、只读Flash写、非法指令、指令限额、时间限额、返回栈不平衡。故障探针必须真的被拒绝才算该自检 PASS；这不等于把生产断言失败当通过。

29项由21项真实生产行为用例和8项指令运行器自检组成。另有2项Python报告I/O契约测试，明确使用模拟执行结果来隔离文件保存错误，不计入真实生产指令用例。

实际验证工具：Python 3.10.11 / Unicorn 2.1.4 / pyelftools 0.32 / Arm GCC 13.3.1 20240614。完整版本、原生命令、源码/ELF摘要及符号来源在每个 `report.json`、`build-command.json`、`elf-info.txt` 内。`build/` 与 venv 被忽略，不进入版本控制；这里保存人工可读的运行快照，重新运行会产生新的原始证据。

实际命令均在仓库根目录执行。`$armCc` 为 README 所述的本机编译器路径；本次解析到 CubeIDE 1.19.0 的 `gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344/tools/bin/arm-none-eabi-gcc.exe`。

正常回归：

```powershell
.\.venv-firmware\Scripts\python.exe tests\firmware\run.py --cc $armCc --group regression --output tests\firmware\build\regression
```

全部测试：

```powershell
.\.venv-firmware\Scripts\python.exe tests\firmware\run.py --cc $armCc --group all --output tests\firmware\build\all
```

报告I/O契约测试：

```powershell
.\.venv-firmware\Scripts\python.exe -m unittest discover -s tests/firmware -p test_runner_errors.py -v
```

独立审查确认生产代码未被替身代算，复放得到29 PASS/1 FAIL；发现“报告保存失败仍返回0”的接入问题，已用失败测试复现并最小修正为返回2，2项契约测试转绿后重跑完整指令测试。

## 保留的生产缺陷

用例：`test_known_coils_padding_must_be_zero`。

前提：线圈0~7都为1，调用真实 `Modbus_Function_1()` 读取从0开始的**1个线圈**。替身只记录生产发送帧，没有替代打包或CRC。

实际响应（数据字节为FF，把多余7个线圈也发出）：

```text
02 01 01 FF 11 8C
```

按 [Modbus Application Protocol V1.1b3 §6.1](https://www.modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf) 独立计算的预期响应（多余高位应补0）：

```text
02 01 01 01 90 0C
```

原因：`BSP/bsp_usart.c` 的 FC01 按完整字节提取数据，没有按请求数量清零末字节剩余位。缺陷已复现；没有修改生产函数，没有放宽断言，也没有把它标记为 XFAIL 后吞掉退出码。

其他观察：HAL_BUSY 情形由测试显式提供已有传输的完成回调，检查方向恢复；不能推断生产代码会自动重发被拒绝的响应。成功发送的全部输出序列在报告中为 DE置位→延时请求20→DMA发送→延时请求10→DE复位；没有只检查最终变量。

## 未执行/不属于通过结论

- IAR正式固件构建未执行：本机9.60.4许可证不可用。
- CubeIDE完整App构建未执行：本次没有修改生产源码或正式工程，只交叉编译所列真实模块的测试ELF。
- 全部真实硬件检查未执行：严格遵守本次不连接硬件的范围。
- Flash保存/回滚/迁移、完整Modbus分发及写参数路径、DAC/PWM、Bootloader、中断抢占/看门狗等未覆盖，详见README。
- QEMU/Renode 未安装或实测。

下一步是单独确认是否修复 FC01 尾位问题并使用保留用例验证；之后按实际风险扩展真实参数存储与 FC03/写参数路径。离线模拟不能代替正式工程构建和上板验收。
