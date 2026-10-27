# STM32 IAR 固件时间戳适配指南（供其他 Agent 使用）

整理日期：2026-10-26。适用环境：Windows、STM32、IAR Embedded Workbench for Arm（EWARM）。

本文提炼自 UMF 项目已有实现，可单独交给其他 Agent。**推荐保留原来的固定文件名，再额外复制一份带日期、时分秒的固件；需要从固件内容追溯编译时间时，再加入内部时间字符串。**

## 1. 目标与执行顺序

1. 检查目标项目的 `.ewp` 工程文件、构建配置、固件输出路径和已有构建后命令。
2. 保留已有固件导出、填充、校验及签名流程，在最终产物生成成功后增加带时间戳的副本。
3. 如需固件内部时间，新增并登记 `timestamp.c`，保留时间字符串。
4. 执行 Rebuild All（全部重新构建），核对文件名、内容一致性及内部时间；对需要支持的每个配置分别验收。

典型结果如下。原固定文件名供烧录工具继续使用：

```text
Firmware.bin
```

新增文件供人工区分和归档，例如：

```text
Firmware_2026-10-26-153045.bin
```

### 两种时间的区别

| 项目 | 文件名时间 | 固件内部时间 |
| --- | --- | --- |
| 生成方式 | 构建后命令调用 PowerShell 的 `Get-Date` | 编译器展开 `__DATE__` 和 `__TIME__` |
| 表示什么 | 复制最终固件的电脑本地时间 | `timestamp.c` 最近一次被编译的时间 |
| 是否改变固件字节 | 复制本身不改变 | 新增时间字符串会改变固件内容 |
| 如何查看 | 直接查看文件名 | 调试器查看变量，或搜索二进制中的字符串 |

二者可能相差数秒；增量构建时可能相差更久。它们都不是单片机运行时间，也不需要 STM32 的 RTC（Real-Time Clock，实时时钟）参与。

## 2. 修改前必须确认的项目事实

Agent 应先读取项目文件和构建日志，不能根据 UMF 示例推断另一个项目的配置：

- 实际使用的 IAR 版本、`.ewp` 文件和配置名称；Debug、Release 等配置可能各有独立命令。
- 最终交付的是 `.bin`、`.hex`，还是经过封装、校验或签名的文件；归档应复制最终交付文件。
- 原有文件的真实输出位置，以及由 Output Converter（输出转换器）还是 Build Actions（构建动作）生成。
- 是否存在 Bootloader（引导程序）、应用分区和参数区；导出范围及填充值是否有协议要求。
- 是否已有同名时间变量或重复的 `timestamp.c`，避免重复定义。

**仅增加文件名时间戳，不需要修改链接地址、向量表、升级协议或固件头。不要把 UMF 的 Flash 范围移植到其他项目。**

## 3. 核心改动：固定文件名 + 时间戳副本

### 3.1 已经能够正常导出固件的项目（优先使用）

修改前：构建成功后只输出固定名称的固件，下一次构建会覆盖它。

修改后：原导出命令保持原意，在最终导出成功后追加复制命令；固定文件和时间戳文件内容完全相同。

下面假设最终固件位于 `.ewp` 所在目录，名称为 `Firmware.bin`。**先将源路径、目标目录、文件名前缀和扩展名替换为目标项目的实际值。**复制命令为：

```bat
powershell.exe -NoProfile -NonInteractive -Command "Copy-Item -LiteralPath '$PROJ_DIR$\Firmware.bin' -Destination (Join-Path '$PROJ_DIR$' ('Firmware_' + (Get-Date -Format 'yyyy-MM-dd-HHmmss') + '.bin')) -ErrorAction Stop"
```

操作方法：

1. 在 IAR 中选中项目及对应配置，打开 **Project → Options → Build Actions**。
2. 找到生成最终固件的命令，在它成功结束后用 `&&` 连接上述复制命令。若后面已有成功标记文件的写入，应把复制放在成功标记之前。
3. 保留原有工作目录、输入输出依赖和构建顺序。新建动作时，按本版本 IAR 的界面选择链接之后执行，并明确依赖最终固件。
4. 若文件由另一个动作或 Output Converter 生成，先核对实际执行顺序；不能仅把两个动作都设为 Post-link 就假定复制一定在导出之后。

这里的命令用于 **IAR 的 Windows 构建命令行**。IAR 会先替换 `$PROJ_DIR$` 等变量；直接粘贴到普通终端时，这些变量不会自动展开。

关键机制：

- `&&`：前一步成功才执行后一步，防止导出失败后归档旧固件。原有脚本也必须正确返回失败退出码。
- `Copy-Item`：复制文件，不移动或重命名原文件，已有烧录工具仍可使用固定名称。
- `-LiteralPath`：源路径按字面解释，避免把方括号等字符当作通配符。
- `-ErrorAction Stop`：复制失败时停止并向构建过程报告失败。
- `-NoProfile -NonInteractive`：不加载个人配置，也不等待交互输入，方便 Agent 和构建工具执行。
- `yyyy-MM-dd-HHmmss`：年、月、日、24 小时制的时、分、秒。`MM` 是月份，`mm` 是分钟。

包含空格和中文的普通路径可按上述引号方式处理；路径若含英文单引号，不能原样套用内嵌 PowerShell 字符串，需由 Agent 改用参数化脚本或正确转义并验证。

### 3.2 尚未导出 `.bin` 的项目

先确认项目需要的文件格式和升级包布局，再配置转换。IAR 的 `ielftool`（ELF 文件处理工具）能够将链接产物转换成原始二进制；ELF 是 Executable and Linkable Format，即可执行与可链接格式。

以下仅为**不要求固定长度或特殊填充**时的最小转换命令，不包含时间戳复制：

```bat
"$TOOLKIT_DIR$\bin\ielftool.exe" --bin "$TARGET_PATH$" "$PROJ_DIR$\Firmware.bin"
```

确认转换结果正确后，再按照 3.1 节追加复制。若原工程已有 CRC（Cyclic Redundancy Check，循环冗余校验）、签名或固件头处理，归档放在这些步骤完成之后。

`.hex` 是包含地址和校验记录的文本格式，`.bin` 是原始字节；不能只改扩展名完成转换。若交付 `.hex`，应复制已经正确生成的 `.hex`，并保留该扩展名。

### 3.3 IAR 变量与 `.ewp` 编辑注意事项

| IAR 变量 | 含义 |
| --- | --- |
| `$PROJ_DIR$` | `.ewp` 所在目录，不一定是仓库根目录 |
| `$TOOLKIT_DIR$` | 当前工具链目录，可据此找到 `bin\ielftool.exe` |
| `$TARGET_PATH$` | 当前链接目标的完整路径，通常是 `.out`，不是自动指向 `.bin` |
| `$BUILD_FILES_DIR$` | 当前配置的构建文件目录；UMF 用它保存构建完成标记 |

IAR 不同版本的 Build Actions 界面和 `.ewp` 结构可能不同，应沿用目标项目已有结构，不要整段替换配置。

在 IAR 界面中输入命令时使用普通的 `&&` 和 `>`。直接编辑 `.ewp` 的 XML（Extensible Markup Language，可扩展标记语言）文本时，必须分别写成 `&amp;&amp;` 和 `&gt;`，命令内的其他 XML 特殊字符也应正确转义。保存后重新解析文件，确认 IAR 能正常打开。

## 4. 可选改动：在固件内部保存编译时间

### 4.1 IAR 专用项目的最小代码

新增 `timestamp.c`，放在项目既有的业务源码目录。核心代码如下：

```c
/* 保留编译时间，即使业务代码没有引用，也写入最终固件。 */
__root const char g_firmware_build_time[] = __DATE__ " " __TIME__;
```

为什么这样写：

- `__DATE__` 是编译日期，`__TIME__` 是编译时刻；相邻字符串在编译时自动连接。
- `const` 将它定义为只读常量，通常放在 Flash 中，不需要在主循环里计算。
- `__root` 是 IAR 的保留属性：即使其他代码没有引用，也保留该对象，避免链接器移除它。前提是这个源文件确实参与编译和链接。
- 字符串本身占 20 字节文本及 1 字节结束符，共 21 字节；最终 Flash 增量还受对齐和链接布局影响，以构建结果为准。

内容示例：

```text
Oct 26 2026 15:30:42
```

必须把 `timestamp.c` 加入 `.ewp` 的实际源码分组；只在磁盘上创建文件不会让 IAR 自动编译。若 `.ewp` 位于 `EWARM/`、源码位于仓库根目录的 `BSP/`，登记节点示例为：

```xml
<file>
    <name>$PROJ_DIR$\..\BSP\timestamp.c</name>
</file>
```

其他目录布局应改用实际相对路径，并确认该文件未在目标配置中被排除。不要在头文件里定义同一全局变量，也不要同时加入包含相同定义的草稿文件。

### 4.2 增量构建与“每次都是新时间”的边界

**`__DATE__`、`__TIME__` 只在该源文件重新编译时刷新。**修改其他 `.c` 后进行增量构建，可能得到新文件名，但内部仍是旧时间；完全没有需要更新的产物时，Make 也可能不执行导出和复制。

默认交付办法：正式出固件时执行 **Rebuild All**，确认日志中确实重新编译了 `timestamp.c`，再检查新产物。UMF 当前没有额外强制刷新时间对象的动作，不应把它描述为“每按一次 F7 都刷新内部时间”。

若目标项目明确要求每次构建都刷新内部时间，需要额外配置强制重编译。IAR 官方提供删除时间对象文件或更新源码修改时间的方法；Agent 应按实际 IAR 版本和构建顺序配置，并通过连续两次构建验证。对象文件名和路径必须从日志确认，不能猜测或批量删除整个对象目录。

文件名仅精确到秒，同一目录、同一前缀在同一秒重复导出可能冲突或覆盖；它不是唯一编号。并行构建或多种硬件配置需要分别使用目录或名称前缀，例如区分 Release、Debug、不同板型，再附加时间。除非用户提出此需求，不必增加其他编号机制。

### 4.3 同时支持 GCC 时的边界

GCC 是 GNU Compiler Collection，GNU 编译器集合。IAR 专用项目直接使用 4.1 节即可。若同一源码还需要 STM32CubeIDE/GCC 编译，不能让 GCC 直接编译 IAR 的 `__root`。

UMF 的跨工具链代码如下：

```c
/* 两种工具链均保留构建时间，避免未引用常量被链接器裁剪。 */
#if defined(__ICCARM__)
__root const char g_firmware_build_time[] = __DATE__ " " __TIME__;
#elif defined(__GNUC__)
__attribute__((used, section(".rodata.firmware_build_time")))
const char g_firmware_build_time[] = __DATE__ " " __TIME__;
#endif
```

GCC 分支还需要链接脚本保留对应节，例如 UMF 在只读数据输出节、匹配通用 `.rodata*` 之前配置：

```ld
KEEP(*(.rodata.firmware_build_time))
```

原因是编译器的 `used` 属性和链接器的节回收是两个环节；只添加 `used` 不能代替链接阶段的保留设置。此段为 UMF 现有实现说明，不是纯 IAR 项目的必要改动。

## 5. UMF 已有实现参考（禁止照抄地址到其他项目）

当前依据：`BSP/timestamp.c`、`EWARM/UMF.ewp`、`EWARM/stm32f103xb_flash.icf`。`.ewp` 记录的 App 工具链版本为 9.60.4.11196；不要仅依据旧说明认定其他项目也使用相同版本。

UMF 的完整构建后命令，以下是 **IAR 界面中的命令形式**：

```bat
"$TOOLKIT_DIR$\bin\ielftool.exe" --bin --fill 0xFF;0x08002400-0x0800F3FF "$TARGET_PATH$" "$PROJ_DIR$\UMF_app.bin" && powershell.exe -NoProfile -NonInteractive -Command "Copy-Item -LiteralPath '$PROJ_DIR$\UMF_app.bin' -Destination (Join-Path '$PROJ_DIR$' ('UMF_app_' + (Get-Date -Format 'yyyy-MM-dd-HHmmss') + '.bin')) -ErrorAction Stop" && echo > "$BUILD_FILES_DIR$/.postbuild"
```

执行顺序与原因：

1. `ielftool` 转成 `.bin`，并用 `0xFF` 补齐本项目应用区的空洞，保持升级包固定大小。
2. 导出成功后复制为带秒级时间的文件，避免普通的后续构建覆盖此前归档。
3. 两步均成功后才写 `.postbuild` 标记。该文件供已有构建依赖跟踪使用，不是待烧录固件；`.ewp` 中保留其输出声明。

这段命令的构建顺序为 `postLink`，工作目录为 `$PROJ_DIR$`，声明的输出为 `$BUILD_FILES_DIR$/.postbuild`。

**UMF 专用范围**是从应用起始地址到末地址（包含末地址），长度为末地址减起始地址再加 1，因此两份文件均为 53,248 字节。`--fill` 负责填充，不是通用的分区裁剪工具；其他项目必须从自己的 ICF（IAR Linker Configuration File，链接配置文件）及升级协议确定范围，避免覆盖 Bootloader 或参数区。

2026-10-26 对工作区已有产物的只读核对结果：`EWARM/UMF_app.bin` 与 `EWARM/UMF_app_2026-10-23-113458.bin` 长度均为 53,248 字节，SHA-256 相同，内部可找到编译时间 `Oct 23 2026 11:34:57`。这验证了已有副本及内部字符串，不表示本次重新构建了固件，也不证明其他项目已完成适配。

## 6. 验收与故障定位

### 必须完成的检查

- [ ] IAR 正式构建成功；目标配置没有新增错误或警告。
- [ ] 原固定名称固件仍然存在，新副本名称包含年月日和时分秒。
- [ ] 对本次同一产物计算 SHA-256，两份结果一致；文件长度符合该项目原来的要求。SHA-256 是 Secure Hash Algorithm 256-bit，用于核对文件内容是否相同。
- [ ] 启用内部时间时，在最终固件中找到对应字符串，并确认它来自本次重新编译。
- [ ] 间隔至少 2 秒，再执行一次 Rebuild All；确认出现不同时间的新文件，上一次归档仍存在。
- [ ] Debug、Release、Bootloader/App 或不同板型中，用户要求支持的每个配置分别通过上述检查。
- [ ] 核对版本、入口、分区、校验及原有打包步骤没有被意外更改；无需为了时间戳功能擅自烧录硬件。

在 Windows PowerShell 中，替换下面示例路径后分别计算哈希；每条命令单独执行：

固定文件的 SHA-256：

```powershell
Get-FileHash -LiteralPath 'D:\YourProject\EWARM\Firmware.bin' -Algorithm SHA256
```

本次时间戳副本的 SHA-256：

```powershell
Get-FileHash -LiteralPath 'D:\YourProject\EWARM\Firmware_2026-10-26-153045.bin' -Algorithm SHA256
```

搜索未加密 `.bin` 中的内部时间，替换实际路径后执行：

```powershell
[regex]::Matches([System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes('D:\YourProject\EWARM\Firmware.bin')), '(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-9][0-9] [0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2}') | ForEach-Object { $_.Value }
```

若最终文件被加密或压缩，应在处理前的链接映像中检查，并按已有流程确认它确实进入最终产物；不能把普通字符串搜索当作通用验证方法。

### 常见问题

| 现象 | 优先检查 |
| --- | --- |
| 没有时间戳文件 | Make 是否实际产生更新、构建动作是否执行、源路径和输出顺序是否正确 |
| 文件名新、内部时间旧 | `timestamp.c` 是否重新编译；先 Rebuild All 验证 |
| 内部找不到时间 | 文件是否加入 `.ewp`、是否被排除、变量是否被链接器移除、是否检查了正确产物 |
| 同名符号重复定义 | 是否同时加入草稿文件，或在头文件中定义了全局变量 |
| 复制失败但误以为构建通过 | 是否保留失败退出码、`&&` 和 `-ErrorAction Stop` |
| 固件大小异常 | 是否误改了导出范围、填充值、输出格式或最终打包步骤 |
| 手改 `.ewp` 后无法打开 | XML 中的 `&`、`>` 是否正确转义，有无破坏原有结构 |

新增归档应沿用目标仓库的产物管理约定；已有 `*.bin`、`*.hex` 忽略规则时不重复添加，也不自动提交构建产物。时间戳是追溯信息，不能替代软件版本或源码提交记录。

## 7. 可直接发给其他 Agent 的任务说明

> 请依据本指南，为当前 STM32 IAR 项目适配固件时间戳。先检查实际 IAR 版本、`.ewp` 配置、最终固件路径和已有 Build Actions，再实施最小修改。
>
> 保留原固定文件名和全部现有打包处理，在最终导出成功后增加 `原前缀_yyyy-MM-dd-HHmmss.原扩展名` 副本。不得直接使用 UMF 的 Flash 地址或固定大小。按当前项目既有规则区分配置及板型，避免输出混淆。
>
> 同时在固件内加入 `__root const char g_firmware_build_time[] = __DATE__ " " __TIME__;`，并把唯一的 `timestamp.c` 加入实际参与构建的 `.ewp` 分组。说明内部时间只在该文件重编译时刷新，默认通过 Rebuild All 出正式固件；不要承诺每按 F7 都刷新。若项目已经有等效实现，应复用，避免重复定义。
>
> 完成后展示修改前的行为、核心修改及原因，按本指南检查构建、文件命名、同次产物 SHA-256 一致性、内部时间以及第二次构建是否保留旧归档。只报告实际执行的结果；许可证或工具缺失时说明尚未通过构建验收。不要擅自烧录、提交或推送。

## 8. 依据与官方资料

- UMF 本地源码和工程：`BSP/timestamp.c`、`EWARM/UMF.ewp`、`EWARM/stm32f103xb_flash.icf`；跨工具链补充依据为 `STM32CubeIDE/STM32F103C8TX_FLASH.ld`。
- [IAR：Using build actions](https://docs.iar.com/ewarm/10.1x/en/ide-project-management-and-building/building-projects/building-a-project/using-build-actions.html)：构建动作、时间戳源码和刷新方法。页面版本为 10.10.x，具体界面应对照目标项目版本。
- [IAR：IDE Project Management and Building Guide for Arm](https://wwwfiles.iar.com/arm/webic/doc/EWARM_IDEGuide.ENU.pdf)：9.50.x 手册，“Using a build action for time stamping”及构建动作设置章节。
- [IAR：C/C++ Development Guide for Arm](https://wwwfiles.iar.com/arm/webic/doc/ewarm_developmentguide.enu.pdf)：`__root` 对未被引用对象的保留语义。
- [IAR：The IAR ELF Tool—ielftool](https://docs.iar.com/ewarm/10.1x/en/iar-c-c---development/iar-utilities/the-iar-elf-tool-ielftool.html)：输出格式、填充和地址范围规则。

本次已在包含空格和中文的临时目录中实际执行 3.1 节复制命令：间隔 2 秒生成两个不同名称的副本，旧副本保留，二者 SHA-256 均与源文件一致；源文件缺失时返回退出码 1。第 6 节时间搜索命令能够提取已有 UMF 固件的内部时间，文中的完整 UMF 命令也已与 `.ewp` 对照一致。

本次交付仅为文档及示例命令核对，未修改 UMF 固件源码或构建配置，未执行 IAR 全量构建和目标板测试。其他项目应以各自实际验收结果为准。
