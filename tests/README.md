# 显示像素回归

使用Python 3和GCC运行，无需连接开发板；`CC`环境变量可指定GCC可执行文件，编译器所在目录需在`PATH`中。

运行全部测试：

```text
python -m unittest discover -s tests -v
```

测试编译真实的`display.c`、`run_display.c`和`ftoa.c`，仅把屏幕总线输出替换为240×240像素接收器，检查数字字形、主页FLOW与TOTAL字母、旧字体、局部更新与完整重绘是否一致，以及内容不变时不重复写屏。

`fixtures/flow_digits_24x32.bitmap`是独立于渲染器保存的字形参考数据：字符顺序`0123456789.-`，每个字形96字节，每行3字节、高位在左。来源与授权见`OLED/flow_digits_24x32.h`和`OLED/licenses/flow_digits-OFL.txt`。

`fixtures/main_labels_24.bitmap`为主页标题参考字模：字符顺序`FLOWTA`，每字72字节，每行3字节；自然字宽依次为15、13、19、23、15、16像素。主页面标题以外的静态内容、原数字字形和辅助页均有独立回归检查。

单位回归验证主页m³/h与m³的上标位置、复用字模及文本宽度，并检查L/h、L/min、L、kg和辅助页的原显示不变。测试顶部单位时避开从y=26开始覆盖其末尾空行的温度字段。

这些测试不验证屏幕总线时序和实物显示效果；固件仍需完成IAR构建及目标板验收。

## 真实目标 CPU 指令回归

新增 [firmware/README.md](firmware/README.md)：将真实生产模块交叉编译为 Cortex-M3 的 ELF，在 Unicorn 中执行；与上面的电脑像素测试分开运行。包含硬件边界替身、按键/标定/传感器/Modbus用例及运行器异常自检。

全部组当前保留一项真实生产缺陷，失败时返回非零；正常回归组与已知缺陷组有明确区分，实际结果见 [firmware/RESULTS.md](firmware/RESULTS.md)。
