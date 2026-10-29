# 显示像素回归

需要 Python 3、Pillow 和 GCC（GNU Compiler Collection，GNU 编译器集合）；`CC` 环境变量可指定 GCC 可执行文件，编译器所在目录需在 `PATH` 中。

运行全部显示测试：

```text
python -m unittest discover -s tests -v
```

`test_industrial_display.py` 编译真实 `display.c`、`run_display.c` 和 `ftoa.c`，并原样提取 `bsp_menu.c` 的绘图函数。屏幕总线替换为 240×240 RGB565 像素接收器，菜单参数替换为仓库默认值，不修改生产接口。

12 项像素测试覆盖四档字模、主界面单位和大数字、43 个菜单页面，以及 128 次局部更新与全屏重绘比对。C 夹具同时断言完整帧恰好写入 57,600 像素、主/辅助页数据未变时零写屏、文字/屏幕地址/像素写入未越界。

另有 `test_screen_rotation.py` 的 4 项功能测试：编译完整生产菜单和参数存储，以及真实 LCD 方向/地址命令、显示初始化和自愈函数，只替换 Flash、总线和绘图边界。覆盖四档方向及 80 像素偏移、按键选择/确认/取消/限位/恢复出厂、三页轮转后的重启读取、旧版参数页兼容、无变化不擦写，以及自愈保留角度。绘图仍由前述像素测试验证；总计 16 项测试。

分页检查从真实像素中的选中条位置反推页首，并统计屏幕总线写入：列表和枚举均按 1～5、6～10、11… 显示，同页移动不清空列表且写入少于 25,000 像素，跨页只清空列表一次，选择移动不触发整屏刷新。另检查跨页上下行选中位置和末页空白，避免只比较最终画面而漏掉重复重绘。

`fixtures/industrial-font-0.png` 至 `industrial-font-3.png` 是独立用 Pillow 12.2.0 从 Montserrat Medium TTF 栅格化的黑白图，分别对应 12/16/32/60px 字体。文字图顺序为 ASCII 32~126、度号、上标 3；数字图顺序为 `0123456789.-`。它们不从生产头文件解码，能够检出位序、跨字节、裁剪和字宽错误。字体来源与授权见 `OLED/licenses/industrial-OFL.txt`。

设置 `UMF_DISPLAY_EXPORT` 为输出目录后，测试会额外导出实际 C 渲染的 PNG 画面；常规测试不会写入仓库。预览参数为离线输入，不代表传感器实测值。

这些测试不验证屏幕总线时序和实物显示效果；构建与容量结果见 [工业黑界面说明](../docs/industrial-black-display.md)，仍需目标板验收。

## 真实目标 CPU 指令回归

新增 [firmware/README.md](firmware/README.md)：将真实生产模块交叉编译为 Cortex-M3 的 ELF，在 Unicorn 中执行；与上面的电脑像素测试分开运行。包含硬件边界替身、按键/标定/传感器/Modbus用例及运行器异常自检。

全部组当前保留一项真实生产缺陷，失败时返回非零；正常回归组与已知缺陷组有明确区分，实际结果见 [firmware/RESULTS.md](firmware/RESULTS.md)。
