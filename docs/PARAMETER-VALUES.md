# 将按值栈参数提升为 MIR 值

2026-11-10，从 `50c831d` 接续，分支 `codex/parameter-values`。本轮减少被修改参数反复读写入参栈槽的开销：完整 TFT **20167 → 20085 B**，USB MSC **5649 → 5625 B**。编译器通用变换同时覆盖普通 ABI2 和原生 ABI4，SDK 源码、工程配置内容相同。

## 实现

原来 MIR 把部分被修改的按值参数当成固定内存对象，每次赋值写回入参栈槽，再由后续表达式读出。调用者不需要这些写回；参数地址未被取用时，函数内部可直接更新一个虚拟值。

新增 `wide_parameter_values.c.inc`，在寄存器分配前执行：

- 按函数签名计算原物理栈槽，挑选至少有一次读、合计至少两次读写的按值参数。排除取址、volatile、atomic 以及不匹配的内存操作。
- 在入口全部 RECEIVE 之后读一次原栈槽，建立可被多次定义的 MIR 值。原读操作变成值复制，原写操作变成赋值。寄存器入参先接收完毕，避免栈读取使用 A/B 时覆盖 ABI2 的首个 32 位参数。
- 由现有存活分析和分配器安排循环更新、分支汇合、跨调用保存和溢出；上一轮副本传播可继续合并新的纯复制。已取址参数继续使用物理栈槽，指针所指对象的读取保持原位置。
- 插入节点时重定位普通 CFG 边和 switch 全部表项。回边仍指向原循环入口，不能重新执行初始化。
- 物理 ABI、传参宽度及偏移不变。包含 1/2/4 字节标量，以及物理 3 字节、MIR 4 字节的指针。

候选仍比较完整发射代码的体积、加权成本，并使用原有循环成本判定。只对本变换允许局部帧略增，沿用分配器的帧上限；其它变换的帧比较规则不变。`Show_Str` 的候选本来因帧 3 → 5 B 被拒绝，但其代码和指令成本都下降，最终允许这项交换。没有加入函数名、工程名或 SDK 特判。

## 完整工程结果

使用冻结输入和正常 Build-CMake 入口，沿用 FunctionSharing、MachineIr、CalleePreservation、NativeIntegerRuntime。源文件 SHA256 全部一致，工程配置按 JSON 内容比较相同；两工程调用保存反馈均为 `FINAL_ACTUAL_CALL_PRESERVATION_VERIFIED`。

| 工程 | 父版 / B | 本轮 / B | 减少 / B | Keil / B | 本轮 / Keil |
| --- | ---: | ---: | ---: | ---: | ---: |
| p080 TFT | 20167 | 20085 | 82 | 18071 | 111.14% |
| p091 USB MSC | 5649 | 5625 | 24 | 4401 | 127.81% |

TFT 距体积 110% 线还差 **207 B**。本轮没有进行完整 TFT/MSC 与 Keil 的运行时间比较。

最终 HEX/RST 中的函数变化如下；调用保存反馈会连带改变未直接采用本变换的函数，因此不把候选字节差直接相加。

| 函数 | 父版 → 本轮 / B |
| --- | ---: |
| Show_Str | 559 → 497 |
| Gui_StrCenter | 143 → 133 |
| LCD_ShowChar | 593 → 587 |
| 共享字体主体 GUI_DrawFont16 | 732 → 728 |
| gui_circle | 216 → 222 |
| USB scsi_in | 172 → 146 |
| USB scsi_read10 | 93 → 95 |

`Show_Str` 局部帧 **3 → 5 B**，溢出值 **2 → 3**。前一版原型 core-01 为 TFT 20145 B、MSC 5625 B；最终 core-02 允许上述帧交换，同时标记提升后的读结果为纯值复制，供现有副本传播处理。未修改其它内存快照的处理规则。

## 实板与编译回归

独立夹具复用已有原生 ABI 参数写入用例，并增加普通 ABI 的参数更新、指针推进、CODE 读取、有符号字节、跨文件取址修改和循环 switch。包含递归、浮点位模式传递、混合宽度、栈局部对象以及高寄存器压力。Python 独立计算期望值。

AI8051U-34K64，40 MHz，Timer0 /12T，UART1 115200。自动定位 STC-Link1D，父版和最终版各下载一次。

| 指标 | 父版 | 本轮 |
| --- | ---: | ---: |
| 测试固件 / B | 4520 | 4160 |
| 普通 ABI pv_ordinary / B | 185 | 129 |
| 普通 ABI pv_switch / B | 241 | 177 |
| 原生 ABI p_eight / B | 173 | 109 |
| 原生 ABI p_loop / B | 83 | 51 |
| 测试组 64 输入中位 tick 汇总 | 13112 | 12119 |
| 控制组同口径 tick | 10180 | 10180.5 |
| 正确性 | 128 项、646 帧通过 | 128 项、636 帧通过 |

夹具体积减少 **7.96%**，测试组耗时减少 **7.57%**；控制组变化约 0.005%，无计时溢出。该结果是相对父版 MX251 的夹具结果，不是 Keil 比较或完整工程性能。

`check-parameter-values.py` 完成 small/large × speed/size 四组编译：检查 ABI2/4 的实际选择、全部物理宽度、真实跳转表、取址参数保留、volatile 参数保留、首参数未使用的情况以及可编码寄存器。实板夹具覆盖初始化顺序、循环回边和 switch 分支重定位。

## 复现与后续

```powershell
python core/sources/compiler/src/mcs251/tests/check-parameter-values.py --sdcc <sdcc.exe> --support <toolchain目录> --output <新目录>
python tools/research/parameter_values_fixture.py create --output <新工程目录>
# 正常 Build-CMake.ps1：MachineIr、CalleePreservation、FunctionGc OFF。
python tools/research/parameter_values_fixture.py capture --hex <firmware.hex> --output <新记录目录>
```

研究目录 `E:/STVAPP/MX251-mainline-20261109/parameter-values`；最终核心 `core-02`，完整工程 `core-02-p080/p091`，最终夹具 `params-parent-02/params-new-02`，实板 `capture-parent/capture-new`，回归 `check-01`。更早的 `params-parent/params-new` 没有 switch 用例，也没有下载，不作为实板证据。输入与产物指纹见 [证据](parameter-values-evidence.json)。公开发布版本未更新。

本轮消除了部分参数栈槽往返。跨函数对象和地址空间信息、热循环跨调用的寄存器保存仍值得继续分析；完整工程的运行时间达线资格仍需另行取得。
