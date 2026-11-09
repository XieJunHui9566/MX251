# 同时存活的值副本合并

2026-11-09，从 `628f5bc` 接续，分支 `codex/value-copy-propagation`。本轮在 MX251 宽 MIR 中加入通用副本传播，完整 TFT 工程 **20377 → 20167 B，减少 210 B**；USB MSC **5651 → 5649 B**。SDK 源码及工程配置内容相同。

## 问题和实现

对照寻址和调用汇编时发现，部分函数在入口把同一个参数复制成多份临时值，分别供后续分支中的调用使用。它们的存活区间重叠，分配器按不同值安排寄存器和溢出槽。`Show_Str` 入口的四套相同参数是明显实例。

新增 `wide_value_copy.c.inc`，在分配前统一处理整数、浮点位模式及相同地址空间的指针副本。沿用现有 MIR 类型、公开 ABI、调用约定和代码发射器。

- 候选是同宽、同符号/类别的纯赋值或转换，目的值只有一个定义。指针必须保持地址空间，固定 ABI 位置的目的值不合并。
- 只处理复制后源值仍然存活的情况。源值已死的复制交给现有寄存器合并，避免无重复存储时改变分配。
- 在完整 CFG 上计算可用副本：入口为空，各前驱取交集；任一端重新定义即取消相等关系，原复制产生新关系。包含循环回边、switch 全部表项和重复目标。
- 仅在关系有效的使用点替换操作数，包括寄存器参数、栈参数和地址索引。所有使用消失后删除纯复制，重新计算存活区间及分配。
- 内存采样节点仍在原位置执行。传播的是已经读出的虚拟值，不缓存内存内容，也不合并 volatile 读写。

新变换复用整函数候选比较，比较字节数、加权指令成本和局部帧，再经过现有循环成本检查。没有加入工程名、函数名或 SDK 特判。

## 完整工程结果

使用同一组冻结输入，启用原有 FunctionSharing、MachineIr、CalleePreservation 和 NativeIntegerRuntime。两工程最终调用保存反馈均为 `FINAL_ACTUAL_CALL_PRESERVATION_VERIFIED`。

| 工程 | 父版 / B | 本轮 / B | 减少 / B | Keil / B | 本轮 / Keil |
| --- | ---: | ---: | ---: | ---: | ---: |
| p080 TFT | 20377 | 20167 | 210 | 18071 | 111.60% |
| p091 USB MSC | 5651 | 5649 | 2 | 4401 | 128.36% |

TFT 距体积 110% 线还差 **289 B**。这不是完整工程运行时间达线结论。

按最终 HEX/RST 核对的函数变化：

| 函数 | 父版 → 本轮 / B |
| --- | ---: |
| Show_Str | 716 → 559 |
| 共享字体主体 GUI_DrawFont16 | 758 → 732 |
| LCD_ShowString | 162 → 146 |
| Test_Triangle | 412 → 408 |
| LCD_ShowChar | 592 → 593 |
| USB usb_read_fifo | 66 → 64 |

`Show_Str` 局部帧 **27 → 3 B**，溢出值 **17 → 2**。函数间调用反馈也会改变保存代码，因此列出最终函数及镜像结果，不把某个候选的减少直接累加。

首版 `core-01` 同时传播源值已死的复制，TFT 为 20145 B、MSC 为 5657 B。MSC 的 `usb_read_reg` 从 legacy 切到 wide：复制消失后寄存器写集合改变，已有后端比较的写集合条件不再允许原 legacy 候选。最终 `core-02` 聚焦同时存活的重复值，保留该小函数原选择；不为 22 B TFT 差额扩大后端选择规则。本轮没有改后端写集合政策。

## 实板与测试

新增独立多分支调用夹具，包含 8/16/32 位重复参数、指针、循环中的源值重写、分支变化和 volatile 前后采样。四个独立被调函数保留真实调用，Python 单独计算结果。

AI8051U-34K64，40 MHz，Timer0 /12T，UART1 115200，自动定位 STC-Link1D。父版和最终版各下载一次。

| 指标 | 父版 | 本轮 |
| --- | ---: | ---: |
| 测试固件 / B | 2896 | 2724 |
| copy_fanout 实际函数 / B | 542 | 372 |
| copy_fanout 局部帧 / B | 46 | 8 |
| 测试组 64 输入中位 tick 汇总 | 14527 | 14127 |
| 控制组同口径 tick | 10166 | 10166.5 |
| 正确性 | 128 项、636 帧通过 | 128 项、632 帧通过 |

夹具体积减少 **5.94%**，测试组耗时减少 **2.75%**，控制组相差约 0.005%，无计时溢出。计时是相对父版 MX251 的夹具结果，不是 Keil 或完整 TFT 工程测试。

`check-value-copy.py` 完成 small/large × speed/size 四组编译，检查调用压力函数实际采用优化，以及 volatile 两次读、一次写仍保留。宿主 C 测试直接包含生产变换文件，检查 13 个图/准入场景；11 个可执行图各比较 256 个输入，包括源值重定义、分支汇合、循环回边、switch 重复目标、栈实参和 ABI 固定目的值。

## 复现及后续

```powershell
python core/sources/compiler/src/mcs251/tests/check-value-copy.py --sdcc <sdcc.exe> --support <toolchain目录> --cc <宿主gcc.exe> --output <新目录>
python tools/research/value_copy_fixture.py create --output <新工程目录>
# 用正常 Build-CMake.ps1 构建，MachineIr、CalleePreservation，FunctionGc OFF。
python tools/research/value_copy_fixture.py capture --hex <firmware.hex> --output <新记录目录>
```

研究目录 `E:/STVAPP/MX251-mainline-20261109/value-copy`；最终核心 `core-02`，工程 `core-02-p080/p091`，夹具 `copies-parent/copies-new`，实板记录 `capture-parent/capture-new`，回归 `check-01`。指纹和输入见 [证据](value-copy-propagation-evidence.json)。公开发布版本未更新。

本轮解决了相同值重复占位，尚未解决被修改参数反复写回栈槽、跨函数对象/地址空间信息丢失，以及完整工程的运行时间比较。后续应继续从这些共同成本入手，不再重复尝试单条无重叠复制传播。
