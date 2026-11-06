# UMF 固件代码质量评审与整改方案

> 日期：2026-11-06
> 范围：`BSP/`、`Core/`、`OLED/`、`newOLED/`、`Boot/` 的第一方源码
> 性质：**非缺陷评审**（命名、耦合、可读性、可维护性、工程卫生），不包含功能 bug
> 构建真相源：**`EWARM/UMF.ewp`（IAR EWARM 8.32）**，见 §2
>
> **行号基准**：文中 `BSP/`、`Boot/` 的行号取自评审时的提交 `4fe88a9`。
> 格式化提交 `64504d5`、`8884901` 之后这些文件行号已整体偏移（`bsp_menu.c` 由 1672 行变 2090 行），
> 检索请优先用符号名（函数名、宏名），行号仅作大致定位。`Core/`、`OLED/`、`newOLED/` 未格式化，行号仍准确。

---

## 0. 一页结论

这个项目的**功能密度很高**（42 屏菜单、6 种交互模式、Modbus 从站、七点标定、Bootloader、参数迁移全部落地），
但**一致性密度很低**。三轮评审（协议层 / 存储层 / UI 层）指向同一个根因：

> **同一语义在仓库里有多处平行实现，而没有机制强制它们一致。**

已有的旁证：

| 语义 | 现存份数 |
|---|---|
| 屏幕渲染 | 2 套（`#if` 互斥，另一套 390 行永不被编译） |
| 屏幕元数据（标题/范围/读写/枚举） | 6 组平行 `switch` + 2 份标题字面量 |
| 参数字段表 | 5 处（struct / 编码 / 解码 / 默认值 / 迁移） |
| 看门狗刷新 | 3 种写法（HAL / 裸寄存器 / 基址强转） |
| 4–20 mA 换算 | 2 处 |
| 2400 L/h 限幅 | 2 处（其一为裸数字） |
| 旧槽读取算法 | 3 处（含一个不参与编译的模块） |
| 毫秒级延时 | 1 个空循环，被 8 处依赖 |

而**强制机制是零**：`.clang-format` 无、`Require Prototypes` 关、MISRA 未跑、警告不升错误、无静态检查脚本。

**因此整改顺序只有一条是对的：先装门禁，再拆重复。** 反过来做，拆完会被下一次复制粘贴污染回去。

---

## 1. 评审口径

### 1.1 读过什么

| 层 | 文件 | 行数 |
|---|---|---|
| 协议 | `bsp_usart.c` / `.h` | 2276 |
| 菜单 | `bsp_menu.c` / `.h` | 1672 |
| 存储 | `param_storage.c` / `.h` | 1472 |
| 显示 | `display.c`、`run_display.c` | 356 / 398 |
| 应用 | `main.c`、`tim.c`、`stm32f1xx_it.c` | 454 / 400 / 307 |
| 驱动/工具 | `key.c`、`mystring.c`、`ftoa.c`、`cal_table.c`、`boot_flag.c` | 约 480 |
| Bootloader | `Boot/Src/bl_*.c` | 约 1200（对照用） |

统计方式：脚本扫描 + 人工逐行核对，每条结论都带 `文件:行号`。

### 1.2 节选量化数据（第一方 12 个文件，7570 行）

| 指标 | 数值 |
|---|---|
| 超过 80 列 | 348 行（4.6%） |
| 超过 100 列 | 79 行 |
| 超过 120 列 | 33 行 |
| CamelCase 函数 / snake_case 函数 | 47 / 262 |
| `bsp_usart.h` 中 `extern` 声明 | 28 |
| 跨模块可写全局对象 | 36 个（+1 只读） |
| `bsp_usart.c` 中 `param_set_*` 调用 | 54 次 |
| `bsp_menu.c` 中 `case SCR_*` | 124 个，分布在 20 个 `switch` |
| 最长函数 | `Modbus_Function_3` 374 行 |

### 1.3 参照的规范

- Linux 内核 [coding-style.rst](https://kernel.ubuntu.com/forgejo/thibf/linux-next/src/commit/9de13951d5c3b424a1ad3767e7bc013831e7ad5d/Documentation/process/coding-style.rst)：函数短小、禁 MixedCase、80 列
- [BARR-C:2018 合规矩阵](https://ldra.com/wp-content/uploads/ldra/BARR-c-2018c-c-Compliance-matrix.pdf)：命名、`_t` 后缀、宏不用语句
- [NASA JPL "Power of 10"](http://spinroot.com/gerard/pdf/P10exp.pdf)：函数 ≤ 约 60 行、禁递归、循环有界
- MISRA C:2012：Rule 2.2（死代码）、8.7（外部链接）、16.4（`default`）、17.7（返回值必须用）、20.7（宏括号）、21.1（保留标识符）
- CERT C：STR31-C、ERR33-C、DCL06-C、PRE31-C
- **项目自身的 `CLAUDE.md`「模块设计原则（强制）」6 条**——评审中 4 条被违反

---

## 2. 已确定的架构决策

### D-1 构建真相源：`EWARM/UMF.ewp`

依据：`STM32CubeIDE/` 工程当前**无法链接**——`BSP/display.c:76` 与 `OLED/oled.c:440` 都定义 `OLED_Init`，
而 `.project` 把 `../BSP`、`../OLED` 整目录链接进工程（`Debug/` 下同时存在 `display.o` 与 `oled.o`）。

由此确立三条硬规则：

| 规则 | 内容 |
|---|---|
| R1 | 固件只通过 `EWARM/Project.eww` 构建；`STM32CubeIDE/` 不产出发版固件 |
| R2 | "哪些 `.c` 属于固件" = `.ewp` 的 `<file>` 清单，磁盘上不在清单内的源码视为死文件 |
| R3 | 构建必须可脚本化（`iarbuild`），不允许只有"点 IDE"这一种方式 |

`STM32CubeIDE/` 处置：**推荐删除**（`syscalls.c`/`sysmem.c`/`startup_stm32f103c8tx.s`/`*.ld` 只服务 GCC，IAR 一个都不引用）；
次选移入 `legacy/` 并加 README 声明非构建入口。若保留，须在 `.cproject` 两个配置的 `excluding` 里同步排除
`BSP/Untitled2.c|BSP/eeprom.c|OLED/oled.c`。

---

### D-2 双显示实现：这是设计选择，不是缺陷

本固件**有意**支持两款屏（SSD1306 128×64 单色 / ST7789 240×240 彩色），实现方式是
`BSP/display_config.h` 的 `DISPLAY_ST7789` 开关 + `bsp_menu.c` 中两段 `#if / #else` 互斥实现。
**这个做法本身成立**：两屏在分辨率、色深、可用字体、行数、刷新策略上都不同，
分开写通常比在一个函数里到处插 `if (is_st7789)` 更清楚——后者是典型的浅抽象。

需要与"无意重复"区分开：

| 类型 | 例子 | 性质 |
|---|---|---|
| **有意的**双实现 | 屏幕渲染两套 | 设计选择；代价是双份维护，收益是各屏都能按自己的特性写 |
| **无意的**重复 | 喂狗 3 种写法、4–20 mA 换算 2 份、参数字段表 5 处 | 缺陷；没有任何收益，只是没统一 |

因此本评审**不把"存在两套渲染实现"记为缺陷**。只有当下面任一条件不成立时，它才退化为风险：

| # | "分别维护"成立的必要条件 | 当前状态 |
|---|---|---|
| 1 | 两套都能编译通过 | ✗ 只有一个 IAR 配置，`DISPLAY_ST7789=0` 从未被验证 |
| 2 | 每次发版都能追溯到"这个固件是给哪种屏的" | ？ 发版记录未标注屏型号 |
| 3 | 改 UI 语义时两份都改，且有提示 | ✗ 无交叉引用注释、无 checklist |
| 4 | 第二套有可运行的验证手段（真机或仿真） | ？ `tests/` 有主机侧仿真，覆盖范围待确认 |

**条件 1 的配套做法（成本最低，不动现有设计）**：

1. 把 `display_config.h` 的宏改成可被编译器覆盖：

   ```c
   #ifndef DISPLAY_ST7789
   #define DISPLAY_ST7789 1   /* 0: SSD1306 128×64；1: ST7789 240×240 */
   #endif
   ```

2. 在 `EWARM/UMF.ewp` 里复制一份配置（Project → Edit Configurations），命名 `UMF_SSD1306`，
   其 C/C++ Compiler → Preprocessor 定义里加 `DISPLAY_ST7789=0`，其余与本配置相同。

3. 构建脚本编两遍：

   ```powershell
   iarbuild EWARM\UMF.ewp -build UMF
   iarbuild EWARM\UMF.ewp -build UMF_SSD1306
   ```

   任一遍失败即阻断。

4. 在两段实现的入口各加一行交叉引用注释，让"改一份忘另一份"这件事在代码里可见：

   ```c
   #if DISPLAY_ST7789
   /* !!! 本段与下方 #else 分支是同一套菜单语义的两份实现。
    *     改动本段时请同步检查 #else 分支（SSD1306 128×64）。 */
   ```

"小屏版本"是已完成、已真机验证过的**冻结基线**，不是待偿的技术债。开发大屏期间不需要改动它，也不应该改它。
上面四条配套的唯一目的，是**确保这份基线还能被重新构建出来**——冻结的前提是"随时能解冻"。

风险的实际形态：开发大屏时若改动共享部分（`display.h`、`run_display.h`、`bsp_menu.c` 里 `#if` 之外的
导航栈/取数/标题表），小屏那份可能失配，而当前构建不会报错。后果是"下次要发小屏版本时才发现编不过"，
不是"现在就是坏的"。

---

## 3. 问题清单

### 3.1 A — 结构性重复（最高优先）

| 编号  | 现象                                                         | 位置                                                                                   | 后果                           | 修复                                            |
| --- | ---------------------------------------------------------- | ------------------------------------------------------------------------------------ | ---------------------------- | --------------------------------------------- |
| A1  | ~~双屏两套实现~~ → **降级为提示**：双实现是设计选择（见 §D-2）。小屏那份是**已完成并验证过的冻结基线**，当前不在构建内 | `bsp_menu.c:689-1508`（`#if` / `#else` / `#endif`）；现役 `render_list` 在 `759`，小屏版在 `1054` | 不是"代码坏了"，而是"改共享部分时不会报错"——失配要等下次构建小屏版本才暴露 | 不改这份代码；只补"可重新构建"的保险（双配置编译） |
| A2  | 条件编译是**两个维度叠加**：屏幕型号（21 处）× 字体开关（旧分支内 10 处），后者直接掐掉绘制语句 | `bsp_menu.c:689-1508`；字体开关见 `1077`、`1128`、`1157`、`1273`、`1376`、`1427` | 改字体裁剪会静默改变界面（`SSD1306_INCLUDE_FONT_7x10` 若关闭，1~4 级列表的标题与行文本都不会绘制） | 字体开关只应影响字体数据，不应决定"画不画" |
| A3  | Modbus FC03 响应组装 374 行，5 段并列 `if` 共享写游标                    | `bsp_usart.c:1103`                                                                   | 6 屏长函数；越界判断 5 种写法；跨区请求静默重复写  | 改表驱动                                          |
| A4  | FC16 写 337 行、FC06 写 272 行                                  | `bsp_usart.c:1514`、`:830`                                                            | 每加一个寄存器动 3 个巨型函数             | 同上，读写共用一张表                                    |
| A5  | `u.num=get(); reg=HI｜LO;` 出现 50+ 次；`str[0]=Rx[5]` 出现 60+ 次 | `bsp_usart.c` 全文                                                                     | 改一处即不一致；无法 grep 出共用路径        | 抽 `float_to_reg_lo/hi()`、`mb_word_to_bytes()` |
| A6  | 参数字段集合在 5 处重复枚举                                            | `param_storage.h:100`、`.c:376/492/641/872`                                           | 加参数改 5 处，漏改＝静默写 0xFF 或静默取默认值 | 单张元数据表驱动                                      |
| A7  | 屏幕标题/范围/读写/枚举散在 6 组平行 `switch`                             | `bsp_menu.c:258/272/307/335/369/463`                                                 | 加一屏改 6 处，"能进不能存"             | 单一 `screen_desc_t` 表                          |
| A8  | 41 条标题 case 中 30 条与列表标签逐字重复，另 6 条为同义变体                     | `bsp_menu.c:463` vs `115-194`                                                        | 同一屏两个名字，两套屏显示不一致             | 标题并入描述符表                                      |
| A9  | 喂狗 3 种写法                                                   | `main.c:188`、`display.c:120/229/276`、`param_storage.c:568`                           | 喂狗散在绘图与存储提交里，看门狗无法证明主循环存活    | 收敛为 `app_supervisor_kick()`                   |
| A10 | 4–20 mA 换算两份                                               | `bsp_usart.c:1967`、`run_display.c:24`                                                | 改一份，显示与 Modbus 回包不一致         | 合并为 `dac_to_current_ma()`                     |
| A11 | 2400 L/h 限幅两处，其一裸数字                                        | `bsp_usart.c:1994`、`run_display.c:12`                                                | 改量程漏一处                       | 单一定义，两处引用                                     |
| A12 | 旧槽读取算法 3 份                                                 | `eeprom.c:162/202`、`param_storage.c:768`                                             | 退役模块靠注释维持第二份实现               | 抽 `legacy_slot_read()`                        |
| A13 | 小端字节装箱样板 4 处                                               | `param_storage.c:249/270/819/617`                                                    | 任一处写错→CRC 误判，表现为"参数丢失"       | 抽 `rd_le16()/put_le32()`                      |
| A14 | 8 张列表表各配手写计数宏，与数组长度无绑定                                     | `bsp_menu.c:122` 等 8 组                                                               | 改表漏改宏即越界/漏项                  | `sizeof(arr)/sizeof(arr[0])`                  |
| A15 | 标题绘制 5 份、列表行 2 套坐标算法                                       | `bsp_menu.c:858/931/1017/1111/1159`、`:535`                                           | 调一处漏四处                       | 抽 `draw_page_title()` / `draw_menu_row()`     |
| A16 | 系数编辑两套坐标公式，小数点 57 写死                                       | `bsp_menu.c:629` vs `936/953`                                                        | 改槽位表后小数点不动                   | 小数点由槽位派生                                      |
| A17 | 同一影子缓冲 `s_aux_text[0]` 被两字段、两字号复用                          | `run_display.c:103` vs `152`                                                         | 差分用新字号栅格化旧串，比较的是伪旧状态         | 一字段一 shadow                                   |
| A18 | 小数位判据写 3 遍                                                 | `run_display.c:116/216/233`                                                          | 两套屏精度不同                      | `auto_decimals()`                             |
| A19 | `CAL_POINT_COUNT` 两份定义                                     | `cal_table.h:13`、`param_storage.c:110`                                               | 包含顺序不同即重定义                   | 由 `param_storage.h` 单一导出                      |
| A20 | 同一 Flash 页地址两份表述                                           | `eeprom.h:89`、`param_storage.c:113`                                                  | 一方改分区另一方不报错                  | 单页号→基址推导                                      |
| A21 | 帧缓冲被当两条临时行缓存复用                                             | `display.c:202`                                                                      | 正确性依赖调用上下文                   | 独立 diff 缓冲或写明契约                               |

### 3.2 B — 耦合与分层

| 编号 | 现象 | 位置 | 后果 | 修复 |
|---|---|---|---|---|
| B1 | 头文件成环 `main.h→bsp_usart.h→usart.h→main.h` | `main.h:34`→`bsp_usart.h:14`→`usart.h:35` | 任何 include 都拿到整个工程 | 公共类型抽 `types.h` |
| B2 | 36 个可写全局当模块 API | `bsp_usart.h:44-150`、`tim.h:42-47`、`main.h:95-124` | 数据所有权无法回答 | 全部收进 getter/setter，以 `param_storage.h` 为范本 |
| B3 | 头文件称"菜单是唯一 setter"，实际协议层 54 处 | `param_storage.h:5` vs `bsp_usart.c` | 契约与代码相反 | 改注释，或加调用域接口 |
| B4 | `DacValueBuf` 三处写 + 宏二次别名 | `main.h:99`、`bsp_menu.c:352`、`bsp_usart.c:1538` | Flash 落盘时机不可审计 | 唯一写入口 |
| B5 | 分层倒置：BSP 调应用层 `Time_Delay()` | `bsp_usart.c:333`（定义在 `main.c:91`） | 任何一层都不可单测 | 时间原语下沉 `bsp_time` |
| B6 | 显示驱动反向依赖参数存储 | `display.c:9/79/86` | 驱动无法脱离应用复用 | `OLED_Init(rotation)` 传入 |
| B7 | 权限规则长在"取显示范围"里 | `bsp_menu.c:417/1454` | 业务约束由显示层决定 | 范围校验移入参数层 |
| B8 | 直接改 HAL 私有状态 `huart->gState`，又拿它当发送完成标志 | `bsp_usart.c:324/1920` | HAL 升级即崩 | 自有 `s_tx_busy` |
| B9 | ISR 调 UI 层，显示计数定义在 `tim.c` | `tim.c:341`、`:32` | ISR 变长、读写方分离 | ISR 只 `s_tick_10ms++` |
| B10 | ISR 调 `UartReceive_IDLE()` 但未 include 其头，靠 `main.h` 传递 | `stm32f1xx_it.c:283/297` | 重构 main.h 即退化为隐式声明 | 显式 include + 只置标志 |
| B11 | 渲染回调契约写"只能绘制"，回调内推进业务状态 | `display.h:62` vs `run_display.c:123/126/53/65` | 30 次/帧，"首遍生效"假设一改就错 | 状态更新移到回调之前 |
| B12 | `menu_process()` 返回值与输出结构矛盾，`menu_st` 从未被读 | `bsp_menu.c:1579`、`main.c:243` | 两个状态通道无权威 | 单一结果通道 |

### 3.3 C — 命名与类型

| 编号 | 现象 | 位置 | 后果 | 修复 |
|---|---|---|---|---|
| C1 | 同族转换函数 5 种写法 | `bsp_usart.c:2220/2230/2247/2264/2168` | 无法按前缀检索 | 统一 `bcd_*` / `fmt_*` |
| C2 | 命名与返回值不符：`BCDTOInt` 返 float、`BCD_TO_LongInt` 返 u64 | `bsp_usart.c:2230/2247` | 调用点强转掩盖单位错误 | `bcd_to_u32/u64()` |
| C3 | 91 行文件里 4 种命名风格 | `mystring.c:34/52/57/67` | 无法建立检索习惯 | 统一 `str_*` |
| C4 | CamelCase 47 个 vs snake 262 个（规范要求 snake） | `main.c:91/368/393/404`、`tim.c:359` | 两套规范并存 | 新代码 snake，旧的顺手改 |
| C5 | include guard 用保留标识符 9 处 | `bsp_usart.h:11`、`main.h:22`、`param_storage.h:8` 等 | MISRA 21.1 违规 | 统一 `MODULE_H` |
| C6 | 类型名无 `_t` 后缀 | `bsp_usart.h:26/33/39` | 与 `SpanTypeDef` 混杂 | `uart_rec_t` 等 |
| C7 | 单位不进名字，4 个时间参数 3 种隐含单位 | `param_storage.h:107-111` | 单位只在注释 | `filter_time_s`、`oled_recovery_100ms` |
| C8 | `XxxAddr` 三个文件三种坐标语义 | `bsp_usart.h:60`、`main.h:92`、`bsp_usart.c:201` | 寄存器错位的历史包袱 | `pdu_off_*` / `reg4x_*` / `sensor_reg_*` |
| C9 | 语句宏伪装成函数，宏参数无括号 | `main.h:111-122` | 看不出在写全局；MISRA 20.7 | `static inline void dac_output_enable()` |
| C10 | 宏别名数组元素并被直接赋值 | `bsp_usart.h:60`、`main.h:99` | 取地址/比较语义不同 | 用 getter/setter |
| C11 | 6 个枚举定义了但字段全用 `uint8_t` | `param_storage.h:16-70` vs `102-105` | 编译期零类型检查 | 字段用枚举类型 |
| C12 | 私有枚举在 `.c`，API 用裸数字+注释 | `bsp_menu.h:18` vs `bsp_menu.c:24` | 编号维护两份 | 枚举进头文件 |
| C13 | 屏幕目标用 `uint8_t`，`0` 当"返回运行页"哨兵 | `bsp_menu.c:111/116` | 每次使用都强转；与 `SCR_MAIN_MENU=3` 冲突 | `screen_t target` + `SCR_RUN_DISPLAY` |
| C14 | 6 处 `(char *)` 强转去掉 const | `bsp_menu.c:861` 等 | MISRA 11.8 | 直接传原指针 |
| C15 | 布尔用 `int`/`uint16_t`/`0-1` 裸值 | `boot_flag.c:79`、`bsp_menu.c:842`、`lcd_row` | 无类型约束 | `<stdbool.h>` |
| C16 | 空字符串当"不显示"哨兵 | `bsp_menu.c:783` | 与"忘记传值"无法区分 | `NULL` 表示不画 |
| C17 | 字体表按枚举值索引，槽位 95/96 裸数字 | `display.c:46/56/61` | 三处须人工一致，换字体静默错字 | 指定初始化 + `_Static_assert` |
| C18 | 同一 API 两义：ST7789 下 `OLED_Present()` 是空函数 | `display.c:261` vs `312` | 无法判断何时上屏 | 头文件注明或改名 |
| C19 | 自造 `c_` 前缀（规范只有 `s_`） | `bsp_menu.c:115` 等 | 常量与状态无法区分 | 写进规范或统一 |
| C20 | include 顺序违反项目规定 | `bsp_menu.c:14-21`、`display.c:1-4` | 掩盖头文件隐式依赖 | 按四段顺序重排 |
| C21 | 用 `DISPLAY_ST7789` 却不 include `display_config.h` | `bsp_menu.c:523` | 配置变动后宏静默为 0 | 显式 include |
| C22 | 头文件暴露死接口/死字段 | `run_display.h:41-42/23/25/36`、`display.h:20-24` | 被误当"已实现功能" | 删除或注明保留原因 |
| C23 | 同一模块两套时基（TIM3 10 ms vs `HAL_GetTick`） | `bsp_menu.c:233` vs `1476` | 阻塞时失效方式不同 | 统一走 `menu_tick_10ms()` |

### 3.4 D — 局部可读性与接口设计

| 编号 | 现象 | 位置 | 后果 | 修复 |
|---|---|---|---|---|
| D1 | 5 个函数超 60 行（最长 374） | `bsp_usart.c:1103/1514/830`、`bsp_menu.c:1275`、`run_display.c:176` | 超出 1~2 屏 | 按 A3/A7 拆 |
| D2 | `switch` 的 `default` 在中间，后面还有 30 个 case | `bsp_usart.c:867` | 误导；MISRA 16.4/16.1 | `default` 移末尾 |
| D3 | 3 个 `switch(f->mode)` 缺 `default` | `bsp_menu.c:1206/814/1628` | 新增模式静默不渲染 | 补 `default` |
| D4 | 8 参数函数，两个同型相邻参数 | `display.c:197`、`run_display.c:57` | 参数顺序错编译器抓不到 | 用 `text_run_t` |
| D5 | 字符宽度 7 手工算居中 7 处 | `bsp_menu.c:859` 等 | 换字体全歪 | 用 `OLED_TextWidth()` |
| D6 | 72 次绘制中 50 次坐标是字面量；裸数字 594 个 | `bsp_menu.c` 全文 | 改边距＝全文件手改 | `layout.h` 具名常量 |
| D7 | 有界/无界字符串函数混用 | `bsp_menu.c:555` vs `1041` | CERT STR31-C | 封装 `str_append()` |
| D8 | 缓冲区偏移与字面量长度手算绑定 | `bsp_menu.c:770` | 改一个字面量即错位 | `strcat` 风格 |
| D9 | `insert_char()` 无容量参数 | `mystring.c:57` | 边界靠调用方自觉 | 增加 `size` 参数 |
| D10 | `u32_to_str_pad()` 在 `width > 11` 时越界写 `tmp[11]` | `mystring.c:67` | 栈溢出 | 入口钳位 |
| D11 | `ftoa()` 上溢分支且 `buf_len < 5` 时一字节不写 | `ftoa.c:69` | 调用方拿到未初始化缓冲 | 入口先写 `'\0'` |
| D12 | 4–20 mA 的 4.0/16.0 裸在表达式里 | `run_display.c:29` | 同文件两套具名标准 | `DAC_CURRENT_MIN_MA` |
| D13 | 空闲超时默认值裸 3000 | `bsp_menu.c:1565` | 30 s 语义只在注释 | 具名宏 |
| D14 | 稀疏表 45 项仅 21 项有值，用 `step != 0` 当哨兵 | `bsp_menu.c:205/297` | 约 0.55 KB Flash 全零 | 加 `valid` 字段 |
| D15 | "弹栈+退出"四行块重复 9 次 | `bsp_menu.c:1320` 等 9 处 | 漏写即停在空栈 | `menu_leave_frame()` |
| D16 | 状态结构填充重复 4 处 | `bsp_menu.c:1579/1586/1593/1638` | 字段增多必漏改 | 单一出口 |
| D17 | 超大对齐赋值块 | `bsp_usart.c:362-424` | diff 噪声淹没真实改动 | 交给 clang-format |

### 3.5 E — 工程卫生与技术债

| 编号 | 现象 | 位置 | 后果 | 修复 |
|---|---|---|---|---|
| E1 | 持久化格式版本号**只写不读** | `param_storage.c:130` 写于 `469`，`page_header_ok` 不读 | 格式演进无判别依据 | 见 P0-5 |
| E2 | `eeprom.c/h` 438 行不参与 IAR 工程、零调用，但 CLAUDE.md 仍把 Page 54~63 当现行映射 | `eeprom.c`、`param_storage.c:113` | 文档与代码相反 | 删除并修文档 |
| E3 | `oled.c` + `oledfont.h` 907 行保留未编译 | `OLED/oled.c` | 仓库噪声；与 `display.c` 符号冲突 | 删除 |
| E4 | `Untitled2.c` 与 `timestamp.c` 定义同一符号 | `Untitled2.c:1`、`timestamp.c:3` | 一旦入工程即 Li006 | 删除 |
| E5 | `FlashProtect()`（RDP）零调用 | `main.c:368` | 读保护从未开启 | 加 `APP_ENABLE_RDP` 开关 + 调用点 |
| E6 | 零消费符号：`leftShift`、`raw2ieee`、`Uart3_Rx_Cnt`、`Timer3Init*`、`Timer3Uart2TimeBase10ms`、`cal_table_init`、`param_storage_get_basic` | 见 P0-2 表 | 死状态被当"在用" | 删除 |
| E7 | `MX_IWDG_Init()` 调两次，中间夹裸寄存器块 | `main.c:143/163/114-118` | 喂狗节律三个来源 | 只留 `iwdg.c` 一处 |
| E8 | `Time_Delay()` 空循环，`CCOptLevel=3` 下被优化掉，却当 RS-485 方向脚延时 | `main.c:91`、`bsp_usart.c` 7 处 | 时序不可验证 | DWT / `HAL_GetTick()` |
| E9 | 每轮主循环重配 PWM（无脏标志） | `main.c:359-360` | 白耗 CPU | 变化才写 |
| E10 | 波特率保存一次 ENTER 落盘两次 | `bsp_menu.c:388` + `393` | 白耗 Flash 寿命 | 删重复调用 |
| E11 | 保存失败仍显示"已保存"（6 处 `(void)param_flush()`） | `bsp_menu.c:364/388/393/1544`、`main.c:149` | 参数静默丢失 | 保留脏位 + 错误帧 |
| E12 | 未实现的量以 `0.00 m/s`、`0.0 Hz` 正常显示 | `run_display.c:153/163`、`278/302` | 现场当真实测量值 | 不显示或 `--` |
| E13 | 语言屏只剩 1 个选项仍可交互 | `bsp_menu.c:518-521`、`param_storage.h:84` | 死交互入口 | 删除或置灰 |
| E14 | 4 处乱码注释（"答"被打成"号"） | `bsp_usart.c:598/602/607/1477` | 读者获得错误信息 | 见 P0-4 |
| E15 | `newOLED/lcd.c`、`lcd_init.c` 为 GBK，其余 UTF-8 | `newOLED/` | 随时变乱码 | 见 P0-4 |
| E16 | 行宽/行尾空格超标 | 全仓 | 阅读与 diff 噪声 | `.clang-format` |
| E17 | 菜单显示 `FW:v1.0.0`，固件头是 v2.5.0（两处） | `bsp_menu.c:775/1133` vs `app_fw_version.h:13` | 界面与真实版本不符 | 见 P0-4 |
| E18 | 编译把关关闭：`CCRequirePrototypes=0`、`CCLangConformance=0`、`CCDiagWarnAreErr=0` | `EWARM/UMF.ewp` | 规范只存在于文档 | 见 P0-1 |
| E19 | 默认值两份：`12100/60000` 在 `param_storage.c:42` 与 `main.c:417` | 同左 | 改默认值漏一处 | 只留 param_storage |
| E20 | RAM 预算无说明：显示层 ≈4.2 KB/20 KB；`s_aux_text[8][32]` 只用 7 行 | `run_display.c:43`、`display.c:16` | 加界面撞 RAM 墙才发现 | 写明预算；`[7][32]` |
| E21 | `APP_FORCE_METER_COEFF` 用运行期 `if` 判编译期常量 | `param_storage.c:726` | 依赖优化器行为 | 改 `#if` |

---

## 4. P0 执行方案（约 2 个工作日）

> 目标：**在不动业务逻辑的前提下**，让"不一致"变成编译错误或脚本错误。
> 每步独立提交，可单独回滚。

### P0-0 基线（1 分钟）

```powershell
git tag p0-baseline
```

### P0-1 格式与编译闸门（约 0.5 天）

**根 `.clang-format`**

```yaml
---
Language: Cpp
BasedOnStyle: LLVM
IndentWidth: 4
TabWidth: 4
UseTab: Never
ColumnLimit: 100
BreakBeforeBraces: Allman
AllowShortFunctionsOnASingleLine: None
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
AllowShortBlocksOnASingleLine: Never
PointerAlignment: Right
AlignConsecutiveAssignments: false
AlignConsecutiveDeclarations: false
AlignConsecutiveMacros: false
MaxEmptyLinesToKeep: 1
SpaceBeforeParens: ControlStatements
SortIncludes: false
IncludeBlocks: Preserve
IndentCaseLabels: false
```

**子目录关掉格式化**（`DisableFormat: true`）：`Core/.clang-format`、`Drivers/.clang-format`、
`OLED/.clang-format`、`newOLED/.clang-format`。

**根 `.editorconfig`**

```ini
root = true

[*]
charset = utf-8
end_of_line = crlf
insert_final_newline = true
trim_trailing_whitespace = true
indent_style = space
indent_size = 4

[*.{md,json,yml,yaml}]
trim_trailing_whitespace = false
```

**IAR 两道闸（分两次落）**

1. C/C++ Compiler → Extra Options：`--c99 --require_prototypes`
   （`.ewp` 对应 `CCRequirePrototypes`、`CCLangConformance` 置 1）
   → 编译，**逐条修 Pe223 隐式声明**（预计 5~15 处，多在 `stm32f1xx_it.c` 这类"靠传递包含"的地方）
2. 警告清零后再加 `--warnings_are_errors`（`CCDiagWarnAreErr=1`）

**格式化落地（避免一次 reformat 全仓）**

```powershell
git switch -c style/clang-format
clang-format -i (Get-ChildItem BSP,Boot -Recurse -Include *.c,*.h).FullName
"$(git rev-parse HEAD)" | Out-File -Encoding ascii .git-blame-ignore-revs
git config blame.ignoreRevsFile .git-blame-ignore-revs
git commit -m "style: 按 .clang-format 统一 BSP/Boot 格式（无逻辑改动）"
```

**验收**

```powershell
clang-format --dry-run --Werror (Get-ChildItem BSP,Boot -Recurse -Include *.c,*.h).FullName
```

#### 执行记录（2026-11-06，分支 `chore/p0-1-format-gate`）

已完成：

| 项 | 结果 |
|---|---|
| 工具 | clang-format 23.1.5（装在仓库内 `.venv-firmware`，不污染系统 PATH） |
| 配置文件 | 根 `.clang-format`、`.editorconfig`；`Core/`、`Drivers/`、`OLED/`、`newOLED/` 各自 `DisableFormat: true` |
| 格式化范围 | `BSP/` + `Boot/` 共 48 个 `.c/.h`，其中 40 个发生变化 |
| IAR 闸门 | `CCRequirePrototypes` 0 → 1（等价 `--require_prototypes`） |
| 闸门待办 | `CCDiagWarnAreErr` 仍为 0，需在告警清零后作为第二步开启 |

与本节初稿的四处差异（均为实测后增补，理由已写入配置文件注释）：

| 键 | 值 | 原因 |
|---|---|---|
| `BreakStringLiterals` | `false` | `display.c` 有 `"\xC2\xB0"` 类十六进制转义，拆分字面量有改变语义的风险 |
| `ReflowComments` | `false` | clang-format 不理解 CJK 显示宽度，重排会打散对齐的中文注释与 ASCII 表格 |
| `AlignEscapedNewlines` | `DontAlign` | 多行宏的反斜杠不重新对齐，减少无意义 diff |
| `AlignConsecutiveMacros` | `Consecutive` | 寄存器地址表按列对齐后可扫读；取 `Consecutive` 而非 `AcrossEmptyLinesAndComments`，使空行分隔的语义分组各自成表 |

两个实测结论：

1. `EWARM/UMF.ewp` 只有**一个**配置（名为 `UMF`，不是 Debug/Release），选项只需改一处。
2. C 方言**已经是 C99**（`IccCDialect = 1`），无需改动；只需补 `--require_prototypes`。

格式化安全性验证（三重，全部通过）：

```text
去空白后 token 序列比对 : 40/40 文件零差异
字符串字面量逐条比对     : 全部一致
clang-format --dry-run   : 无未收敛文件（幂等）
```

一处人工干预：`BSP/bsp_usart.h` 有 4 条超长尾注释（`FlowRateValue` / `BaudRateReg` / `OledRecoveryAddr` 宏与 `Cumulativeflow` 声明）。
clang-format 会把它们折成带 `\` 续行的三行写法（`#define FlowRateValue \` / `InputBuffer \` / `[0] // ...`），
语义合法但可读性更差。处理方式：把这 4 条注释移到声明上方，宏保持单行。**只移动注释位置，未改代码与注释文字。**

尚未完成：IAR 实机全量编译验证（本机无 IAR 环境）。预期会暴露 5~15 处因 `--require_prototypes`
新增的 Pe223 隐式声明告警，逐条补 `#include` 即可。

### P0-2 清理死文件与死代码（约 0.5 天）

前置：按 §2 决策确认 `STM32CubeIDE/` 已删除或已同步排除。

```powershell
git rm BSP/Untitled2.c
git rm BSP/eeprom.c BSP/eeprom.h
git rm OLED/oled.c OLED/oled.h OLED/oledfont.h
```

符号级删除：

| 符号 | 位置 | 说明 |
|---|---|---|
| `leftShift` | `mystring.c:34`、`mystring.h:20` | 零调用 |
| `raw2ieee` | `bsp_usart.c:2168`、`:224` | 零调用 |
| `Uart3_Rx_Cnt` | `bsp_usart.h:41` | 仅一处定义 |
| `Timer3InitEnabled` / `Time3InitTimeBase` / `Timer3Uart2TimeBase10ms` | `tim.c:29-31`、`tim.h:43-45`、`tim.c:345/347-348` | 只增不用 |
| `cal_table_init` | `cal_table.c:8`、`cal_table.h:16` | 空函数 |
| `param_storage_get_basic` | `param_storage.c:1037`、`.h:167` | 零调用 |
| `param_set_display_rotation` | — | **保留**：`tests/rotation_sim.c` 在调用 |
| `FlashProtect` | `main.c:368` | **保留 + 加开关**：见下 |

```c
/* Core/Inc/main.h → USER CODE BEGIN Private defines */
#define APP_ENABLE_RDP   0   /* 0=不使能(可调试)；1=量产出厂镜像必须为 1 */

/* Core/Src/main.c → USER CODE BEGIN 2，param_storage_init() 之前 */
#if APP_ENABLE_RDP
    FlashProtect();
#endif
```

文档同步：`CLAUDE.md` 的 EEPROM 页表整段替换为"Page 61~63 三页轮转，见 `param_storage.c:113`"；
`README.md` 文件结构删除 `eeprom.*` / `oled.*`。

**验收**：`scripts/check_ewp_sync.ps1` 通过；map 文件 Flash 占用下降约 1.5~2 KB。

### P0-3 构建清单一致性门禁（约 0.5 天）

`scripts/check_ewp_sync.ps1`：

```powershell
# 校验 EWARM/UMF.ewp 的源文件清单与磁盘内容是否一致
$xml = Get-Content 'EWARM\UMF.ewp' -Raw

$listed = [regex]::Matches($xml, '<name>\$PROJ_DIR\$\.\.\\([^<]+\.(?:c|s))</name>') |
          ForEach-Object { $_.Groups[1].Value -replace '\\', '/' } |
          Where-Object { $_ -match '^(BSP|Core/Src|Boot/Src|newOLED|OLED)/' } |
          Sort-Object -Unique

$onDisk = git ls-files BSP Core/Src Boot/Src newOLED OLED |
          Where-Object { $_ -match '\.(c|s)$' } |
          Sort-Object -Unique

$missing = $onDisk | Where-Object { $listed -notcontains $_ }
$orphan  = $listed | Where-Object { $onDisk -notcontains $_ }

if ($missing) { "未加入 IAR 工程（不会被编译）:"; $missing; exit 1 }
if ($orphan)  { ".ewp 指向不存在的文件:"; $orphan;  exit 1 }
"ewp 同步检查通过（$($listed.Count) 个源文件）"
```

固定构建命令（供 CI 与本脚本配套）：

```powershell
& "C:\Program Files\IAR Systems\Embedded Workbench 8.4\arm\bin\iarbuild.exe" `
    EWARM\UMF.ewp -build Debug -log warnings
```

### P0-4 乱码、版本号、编码（约 0.5 天）

**四处乱码替换**

| 位置 | 改为 |
|---|---|
| `bsp_usart.c:598` | `// 接收完成标志=1 时处理，否则不应答` |
| `bsp_usart.c:602` | `// 地址不匹配：不应答` |
| `bsp_usart.c:607` | `// CRC 校验错误：不应答` |
| `bsp_usart.c:1477` | `/* 对应 MODBUS 04 功能码：读输入寄存器 */` |

**版本号单源**（在 `bsp_menu.c` 内实现，不动头文件）

```c
#include "app_fw_version.h"

static unsigned char s_fw_ver_text[16];   /* "FW:v2.5.0" + NUL */

static void fw_version_text_build(void)
{
    uint32_t v     = APP_FW_VERSION;
    uint8_t  major = (uint8_t)((v >> 16) & 0xFFu);
    uint8_t  minor = (uint8_t)((v >>  8) & 0xFFu);
    uint8_t  patch = (uint8_t)( v        & 0xFFu);
    unsigned char *p = s_fw_ver_text;

    *p++ = 'F'; *p++ = 'W'; *p++ = ':'; *p++ = 'v';
    *p++ = (unsigned char)('0' + (major / 10u) % 10u);
    *p++ = (unsigned char)('0' + (major % 10u));
    *p++ = '.';
    *p++ = (unsigned char)('0' + (minor / 10u) % 10u);
    *p++ = (unsigned char)('0' + (minor % 10u));
    *p++ = '.';
    *p++ = (unsigned char)('0' + (patch / 10u) % 10u);
    *p++ = (unsigned char)('0' + (patch % 10u));
    *p   = '\0';
}
```

`menu_init()` 末尾调用一次；`bsp_menu.c:775/1133` 改为传 `(const char *)s_fw_ver_text`。

**编码统一**

```powershell
$gbk  = [System.Text.Encoding]::GetEncoding(936)
$utf8 = New-Object System.Text.UTF8Encoding($false)
foreach ($f in @('newOLED\lcd.c','newOLED\lcd_init.c')) {
    $p = (Resolve-Path $f).Path
    $t = [System.IO.File]::ReadAllText($p, $gbk)
    [System.IO.File]::WriteAllText($p, $t, $utf8)
}
```

转换后必须肉眼确认注释未变问号（单向操作，错了只能回滚）。

**编码门禁 `scripts/check_encoding.ps1`**

```powershell
$bad = @()
Get-ChildItem BSP,Core,Boot,OLED,newOLED -Recurse -Include *.c,*.h |
  Where-Object { $_.FullName -notmatch '\\Drivers\\' } |
  ForEach-Object {
    $bytes = [System.IO.File]::ReadAllBytes($_.FullName)
    try { [void](New-Object System.Text.UTF8Encoding($false,$true)).GetString($bytes) }
    catch { $bad += $_.FullName; return }
    if ([System.IO.File]::ReadAllText($_.FullName) -match [char]0xFFFD) { $bad += $_.FullName }
  }
if ($bad) { $bad | ForEach-Object { "非 UTF-8 或含替换字符: $_" }; exit 1 }
"编码检查通过"
```

### P0-5 页头格式版本回读（约 1~2 小时）

现状：`PG_OFF_STATE`（`0x006`）低字节是 state、高字节是 `fmt_ver`；`0x007` 只写不读。

```c
static int page_header_ok(uint32_t base, int check_data, uint16_t *seq_out)
{
    uint8_t  bytes[12];
    uint32_t i;
    uint16_t state_word;

    if (flash_rd32(base + PG_OFF_MAGIC) != PG_MAGIC) { return 0; }

    state_word = flash_rd16(base + PG_OFF_STATE);
    if ((uint8_t)state_word != PG_STATE_VALID)       { return 0; }

    /* 格式版本：0x01=当前；0xFF=早期固件未写该字节，按 v1 兼容；其它未知版本拒绝 */
    if (((uint8_t)(state_word >> 8) != PG_FMT_VER) &&
        ((uint8_t)(state_word >> 8) != 0xFFu))       { return 0; }

    for (i = 0; i < 10u; i += 2u) { /* 原样：拼 0x000~0x009 并校验 hdr_crc */ }
    ...
}
```

> **`0xFF` 兼容分支不可省**：缺了它，现网所有旧页都会被判无效 → `param_load_defaults()` → 用户参数全部回默认值。

可选：新增 `PARAM_STATUS_FMT_MISMATCH (1u << 3)` 并映射到 Modbus 40130，供上位机判断"设备曾装过更新固件"。

**验证**：写参数 → 断电 → 上电 → Modbus 读 40025/40029 未被重置；主机侧用 `tests/firmware` 注入
`byte[0x007] = 0x02 / 0xFF` 验证拒绝与兼容两条路径。

### P0-6 一次性验收

```powershell
$ok = $true
& clang-format --dry-run --Werror (Get-ChildItem BSP,Boot -Recurse -Include *.c,*.h).FullName
if (-not $?) { "格式失败"; $ok = $false }
& .\scripts\check_encoding.ps1; if (-not $?) { "编码失败"; $ok = $false }
& .\scripts\check_ewp_sync.ps1; if (-not $?) { "ewp 失败"; $ok = $false }
if (Select-String 'BSP\*.c','Core\Src\*.c' -Pattern 'FW:v[0-9]' -Quiet) { "版本号仍硬编码"; $ok = $false }
if (-not $ok) { throw "P0 未完成" }
```

---

## 5. P1–P3 路线

| 阶段 | 内容 | 覆盖条目 | 验收 |
|---|---|---|---|
| **P1**（1~2 周） | Modbus 表驱动；`bsp_usart.c` 拆 `uart_transport` / `modbus_slave` / `ufl_protocol` / `flow_filter`；抽 `float↔2regs`、`mb_word_to_bytes` | A3,A4,A5,B3 | 三个巨型函数 <120 行；`grep -c param_set_ bsp_usart.c` = 0 |
| **P1**（3 天） | 喂狗 / 时间原语 / 默认值单点收敛 | A9,B8,E7,E8,E9,E19 | `grep -rn "IWDG->KR"` 只剩 1 处 |
| **P2**（1 周） | 显示 HAL，删 21 处 `#if`；`render_*` 合成一份 | A1,A2,A15,A16,B6,B11,C18 | `grep -c "#if DISPLAY_ST7789" BSP/*.c` = 0 |
| **P2**（1 周） | 屏幕描述符表替代 6 组 `switch` | A7,A8,A14,C12,C13,D14 | `bsp_menu.c` < 900 行；加一屏只改表一行 |
| **P3**（持续） | 参数表单一真源 | A6,A12,A13,B2,B4 | 加参数只改一张表 |

**只做三件的版本**：P0 全部（2 天）→ A3/A4 表驱动（1 周）→ A1/A2 显示 HAL（1 周）。

---

## 6. 附录

### 6.1 已经做对的部分（整改基线）

这些是团队已经具备的能力，整改目标是**把它们变成模板**，而不是重新学：

- `BSP/key.c`：120 行，`s_` 前缀静态状态、内部函数全 `static`、注释讲清引脚命名反转的坑 —— 全仓最干净
- `BSP/run_display.h`：`run_display_input_t` 全 const 指针入参，正是 `CLAUDE.md` 第 2 条的范式
- `BSP/boot_flag.h`：用负长度数组做编译期布局断言（手法可换成 C11 `_Static_assert`，思路正确）
- `Boot/Src/bl_*.c`：`bl_` 前缀分层、一模块一 `.h`、`static` 收敛、常量写法统一 —— **整体高于 App 侧**
- `BL更新日志.md`、`UMF_Bootloader_Upgrade_Plan.md` 等文档的契约意识

> 结论：问题不在能力，在**没有把"做对的那一份"固化成门禁**。

### 6.2 关键实测数据

| 项 | 值 |
|---|---|
| 最长函数 | `Modbus_Function_3`（`bsp_usart.c:1103`）374 行 |
| 次长 | `Modbus_Function_10` 337 行；`Modbus_Function_6` 272 行 |
| 菜单 | `bsp_menu.c` 1672 行 / 52 函数 / 20 个 switch / 124 个 `case SCR_*` |
| 存储 | `param_storage.c` 1472 行 / 37 getter + 34 setter / 其中 30 个 setter 恒返 `HAL_OK` |
| 全局 | 可写 36 个 + 只读 1 个 |
| IAR 工程未编译的源码 | 约 1600 行（`eeprom.*`、`oled.*`、`Untitled2.c`） |
| CubeIDE 多发散 | `Debug/` 下 `eeprom.o`、`oled.o`，与 IAR 清单不一致 |

### 6.3 复现评审的方法

```powershell
# 行宽 / 行尾空格 / Tab 统计
Get-ChildItem BSP -Include *.c,*.h -Recurse | ForEach-Object {
    $l = Get-Content $_.FullName
    [pscustomobject]@{
        File = $_.Name
        Over80 = ($l | Where-Object { $_.Length -gt 80 }).Count
        Trailing = ($l | Where-Object { $_ -match '\s+$' }).Count
    }
}
```

```powershell
# 死符号速查（定义/声明各 1 处 = 无调用者）
foreach ($s in 'leftShift','raw2ieee','param_storage_get_basic','cal_table_init') {
    "{0}: {1}" -f $s, (Select-String BSP\*.c,BSP\*.h,Core\Src\*.c -Pattern $s | Measure-Object).Count
}
```
