# 值定义与局部存储优化

2026-11-10，基于 `69cf94e`，分支 `codex/value-webs`。完整 TFT **19968 → 19707 B**，相对 Keil **109.05%**；USB MSC **5621 → 5613 B**，相对 Keil **127.54%**。全部改动位于编译器及其测试，SDK 输入和工程配置相同。

## 实现

### 定义、使用与局部标量

新增 `wide_value_webs.c.inc`，按完整 MIR CFG 的定义/使用关系拆分寄存器分配身份。能到达同一活跃使用点的定义合并为一个 web，包括循环回边和 switch 的所有表项；互不相连的定义使用不同身份。自更新指令的输入和输出可以使用不同身份。固定 ABI 值保持原身份，容量不足时保留原表示。

这是无需添加边上复制的定义拆分，**不是完整 SSA/phi 后端**。它解除一部分无关生命期之间的分配约束，不把所有分支定义强行拆开。

新增 `wide_definition_copies.c.inc`，将可用副本事实绑定到每条定义，不再要求目的值只有一次赋值，也不要求源值在复制后继续活跃。源或目的重新定义会撤销对应事实，汇合点取交集；传播后重新计算活跃性，移除已无用途的虚拟值赋值。真实内存读写保留。

新增 `wide_private_values.c.inc`，将仅剩完整直接访问的私有局部标量提升为 MIR 值，移除对应对象槽。剩余取址、部分访问、volatile 等用途继续使用对象。提升最后一个对象后保留原有的对象分配候选范围，避免因为对象数变为零而漏选已有方案。

局部提升作为原流水线的外层候选比较。实验中把它嵌在参数地址折叠内部，会改变后者的帧比较基线，使更小代码被拒绝；该安排导致 TFT 增大，已撤回。最终保留原流水线作为完整对照，比较实际发射的体积、加权成本及现有循环成本。

### 局部常量写入

新增 `wide_stack_constants.c.inc`：相邻且中间没有读取的普通局部常量写入按字打包，重复字值复用 WR20，奇数尾字节使用 A。支持零值和非零常量，保留大端布局；volatile、指针物理存储以及其他节点结束这一序列。没有增加清零库调用或循环。

### 调用处重建栈地址

新增 `wide_stack_call_addresses.c.inc`：沿单定义、支配关系明确的局部对象地址链传播对象与偏移，在普通 ABI 调用实参中保留该身份。发射实参时，依据当前帧、调用保存量和已压入实参数计算实际 SPX 偏移，按原三字节 ABI 传递。

因此不必为了稍后的调用一直占用四字节虚拟寄存器。对象本身及被调函数的读写不变；递归、多个栈指针实参和可变参数使用同一机制。此变换同样比较完整函数候选。

## 完整工程结果

沿用 FunctionSharing、MachineIr、CalleePreservation、NativeIntegerRuntime 和冻结 SDK 输入；最终调用保存反馈通过。

| 工程 | 父版 / B | 本轮 / B | 减少 / B | Keil / B | 本轮 / Keil |
| --- | ---: | ---: | ---: | ---: | ---: |
| p080 TFT | 19968 | 19707 | 261 | 18071 | 109.05% |
| p091 USB MSC | 5621 | 5613 | 8 | 4401 | 127.54% |

TFT 比体积 110% 的整数上限低 171 B。此处只确认体积，尚无完整工程相对 Keil 的耗时对照。

最终 HEX/RST 的主要函数变化：

| 函数 | 父版 / B | 本轮 / B |
| --- | ---: | ---: |
| Test_Read | 580 | 427 |
| Fill_Triangel | 754 | 714 |
| LCD_ShowChar | 593 | 573 |
| gui_circle | 222 | 208 |
| LCD_Read_ID | 83 | 72 |
| scsi_out | 133 | 117 |

MSC 的 `usb_isr` 166 → 158 B，但调用保存反馈使 `usb_read_reg` 26 → 42 B；净收益为 8 B，不能把函数局部节省全部计为工程收益。跨函数保存策略仍是后续方向。

## 语义与实板

- 主机执行生产定义拆分/副本传播代码：20 组结构及 CFG 用例，每组可执行图对照 256 个输入，包括重新赋值、分支、循环、重复 switch 目标、固定 ABI 和容量边界。
- `check-value-copy.py`、`check-parameter-addresses.py`、`check-value-storage.py` 分别完成 small/large × speed/size 四组编译。
- 新增局部存储用例覆盖 31 字节清零、非零及奇数长度初始化、循环交换、switch、递归、多个指针实参、可变参数和 volatile。编译测试另将 volatile 用例的字节余数换成掩码，使该函数进入宽 MIR，确认原字节事务未被合并；实板用例保留余数及原后端路径。

AI8051U-34K64，40 MHz，Timer0 /12T，UART1 115200；自动寻找 STC-Link1D。本轮父/新版实际下载并采集：

| 夹具 | 固件 / B | 测试组 tick 汇总 | 控制组 tick 汇总 | 结果 |
| --- | ---: | ---: | ---: | --- |
| 参数、取址和新增局部存储 | 8284 → 7862 | 41135 → 40124 | 10193 → 10167 | 父/新各 128 项通过，602/608 帧 |
| SDK 三角形内核 | 2274 → 2133 | 10235 → 10193 | 10160 → 10125 | 父/新各 128 项通过，630/638 帧 |

按每个输入的多帧中位数求和，无计时溢出。组合夹具体积减少 **5.09%**、测试组耗时减少 **2.46%**；三角形夹具体积减少 **6.20%**、耗时减少 **0.41%**。控制代码也由各自版本编译，其变化单列，不将静态加权成本称为实测周期。

独立用例中，`vs_values` 的局部提升及副本传播候选 287 → 195 B，帧 4 → 0 B；`vs_buffers` 的调用地址重建候选 568 → 457 B，帧 58 → 40 B、溢出值 5 → 0。它们证明机制能作用于独立代码，但不替代上表完整镜像与实测结果。

## 复现与接续

研究目录：`E:/STVAPP/MX251-mainline-20261110/value-webs`。最终开发工具链为 `core-04`，`core-04-p080/p091` 为完整工程，`storage-parent/new`、`triangle-new` 为夹具，`*-capture-*` 保存烧录和原始串口记录。

```powershell
python core/sources/compiler/src/mcs251/tests/check-value-copy.py --sdcc <sdcc.exe> --support <toolchain> --cc <gcc.exe> --output <新目录>
python core/sources/compiler/src/mcs251/tests/check-value-storage.py --sdcc <sdcc.exe> --support <toolchain> --output <新目录>
python tools/research/value_storage_fixture.py create --output <新工程目录>
python tools/research/value_storage_fixture.py capture --hex <firmware.hex> --output <新记录目录>
```

分阶段结果：仅 web 拆分为 TFT 19960 B / MSC 5621 B；错误候选嵌套方案 TFT 20059 B；修正次序、加入定义副本传播和常量写入后 19747 / 5613 B；再加入栈地址重建后 19707 / 5613 B。没有把未采用版本烧到板上。

产物、输入和测试记录的指纹见 [证据](value-storage-evidence.json)。开发工具链之后仅统一了修改行的换行符，机器语义不变。公开发布工具链未更新。硬件最后运行 `triangle-new`，未恢复旧固件。
