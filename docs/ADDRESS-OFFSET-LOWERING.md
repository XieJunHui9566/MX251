# 24 位地址偏移的原生表示

2026-11-09，从 `fe5b795` 接续，分支 `codex/address-offset-lowering`。完成上一轮寻址研究的第一步：让仅用于平坦指针加减的 24 位整数临时值进入宽后端。TFT 完整工程 **20485 → 20377 B，减少 108 B**；独立寻址夹具减少 605 B，实板寻址用例耗时减少 33.07%。SDK 源码和配置未改。

## 实现

公共中端会把一些指针偏移收缩为 `_BitInt(24)`，例如 `p + i * 2` 中，先按 C 的 16 位整数语义计算，再转换为地址偏移。宽后端原来不接受三字节整数，因此整个函数退回旧后端。

新增 `wide_address_offset.c.inc`，在导入操作数时识别以下数据流：临时值的每个定义都是受支持整数到 24 位整数的转换，所有使用都只处于三字节平坦指针加减的右操作数。不能取址、存入真实对象或继续用作整数比较/返回值。分析读取当前函数全部相关基本块，不修改公共 iCode 的类型和指令。

- 所有定义的源宽度、符号一致时，保留原 8/16/32 位表示；在地址使用处按源符号扩展。
- 不同宽度或符号的定义汇合时，在每个定义处扩展到 32 位，保留各条路径的正确位模式。
- 指针运算仍复用现有 24 位地址规范化，CODE/XDATA/generic 的访问方式和公开三字节指针 ABI 不变。

其依据是低 24 位地址相同：`(base ± cast24(x)) mod 2^24` 等于 `(base ± extend(x)) mod 2^24`。该等式只用于地址消费；普通 24 位整数比较、符号扩展、返回或存储仍需要其整数语义，继续使用已有后端。

本轮没有实现跨函数对象摘要、逐对象克隆或循环游标改写。新增能力按类型和使用关系选择，未按 SDK 或函数名选择。

## 完整工程

两侧使用相同输入哈希、相同工程快照与选项，继续启用函数共享、MachineIr、CalleePreservation 和相应的原生整数运行库。最终调用保存反馈通过。

| 工程 | 父版 / B | 本轮 / B | 减少 / B | Keil / B | 本轮 / Keil |
| --- | ---: | ---: | ---: | ---: | ---: |
| p080 TFT | 20485 | 20377 | 108 | 18071 | 112.76% |
| p091 USB MSC | 5651 | 5651 | 0 | 4401 | 128.40% |

TFT `Gui_Drawbmp16` 从 261 降到 161 B；调用写集合改变后，`Rotate_Test` 的保存代码减少 8 B，形成整工程 108 B 减少。TFT 距体积 110% 线还差 499 B。没有完整工程运行时间结论，也没有扩大 SDK 矩阵。

第一版统一采用 32 位偏移，TFT 为 20371 B；最终版保留匹配的窄源表示后为 20377 B，寄存器分配及调用保存使这个工程多了 6 B。这不是所有函数都从窄表示获益的证明。最终记录以本轮 `core-02` 为准，第一版作为对照保留。

## 实板与回归

复用并扩展上一轮指针索引夹具：XDATA 字节/字数组、CODE 表、有符号正负索引、volatile、读写、显式字节回绕；新增 32 → 24 位转换、不同高字节但相同低 24 位、指针减法、同一偏移多次使用、不同宽度分支汇合和有符号字节来源。Python 单独计算结果。

AI8051U-34K64，40 MHz，Timer0 /12T，UART1 115200，STC-Link1D 自动定位。父/新版本各烧录一次最终夹具。

| 指标 | 父版 | 本轮 |
| --- | ---: | ---: |
| 测试固件 / B | 3658 | 3053 |
| 寻址用例，64 输入中位 tick 汇总 | 10064 | 6736 |
| 控制组，同口径 tick | 10172.5 | 10174.5 |
| 正确性 | 128 项、513 帧通过 | 128 项、636 帧通过 |

测试固件体积减少 **16.54%**，寻址计时减少 **33.07%**，控制组相差约 0.02%，无计时溢出。此处是相对父版 MX251 的独立夹具结果，不能替代 Keil 或完整 TFT 工程计时。

夹具中部分实际链接函数体积：

| 通用模式 | 父版 → 本轮 / B |
| --- | ---: |
| 有符号索引读取 | 58 → 41 |
| 嵌套索引 | 85 → 51 |
| 同一 24 位偏移的加/减两次读取 | 131 → 50 |
| CODE 读取 | 54 → 42 |
| 16/32 位来源的分支汇合 | 86 → 55 |
| 有符号字节偏移 | 62 → 39 |
| 索引写入 | 63 → 42 |

`check-address-offset.py` 的 small/large × speed/size 四组通过，检查 8/16/32 位内部表示、混合符号定义、CODE 读取和普通整数用途不被接纳。`check-pointer-stride.py` 四组通过，继续确认上一轮前端修复保留完整的索引表达式宽度。

## 复现与后续

```powershell
python core/sources/compiler/src/mcs251/tests/check-address-offset.py --sdcc <sdcc.exe> --support <toolchain目录> --output <新目录>
python core/sources/compiler/src/mcs251/tests/check-pointer-stride.py --sdcc <sdcc.exe> --support <toolchain目录> --output <新目录>
python tools/research/pointer_stride_fixture.py create --output <新工程目录>
# 用正常 Build-CMake.ps1 构建，启用 MachineIr、CalleePreservation，FunctionGc OFF。
python tools/research/pointer_stride_fixture.py capture --hex <firmware.hex> --output <新记录目录>
```

研究目录 `E:/STVAPP/MX251-mainline-20261109/address-offset`：最终核心 `core-02`，工程 `core-02-p080/p091`，硬件构建 `stride-parent-02` / `stride-new-02`，串口记录 `capture-parent` / `capture-new`。此前无 `-02` 的夹具构建缺少最后增加的两类输入，没有下载，不作实板结果。全部指纹见 [证据](address-offset-lowering-evidence.json)。

下一步继续依据 [Keil/STC-IDE 研究](STCIDE-KEIL-DEEP-REVIEW.md)处理地址形成与访问间的搬运，再接入调用点的对象/地址空间信息。此次 MSC 不变，说明“允许 24 位偏移进入宽后端”只解决了其中一个缺口，不能将其当作整个寻址主线已经完成。
