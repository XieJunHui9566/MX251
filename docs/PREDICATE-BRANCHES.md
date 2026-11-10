# 掩码条件直接分支

2026-11-10，基于 `0c184cb`，分支 `codex/predicate-branches`。本轮将只用于条件跳转的常量掩码和取位直接发射为字节判断或位跳转。修改位于 MX251 宽后端，没有 SDK 源码修改或工程、函数名匹配。[构建与实板证据](predicate-branches-evidence.json)记录输入和产物指纹。

## 完整工程

沿用上轮冻结输入、small/speed、FunctionSharing、MachineIr、CalleePreservation、NativeIntegerRuntime、普通调用反馈与函数 GC。

| 工程 | 父版 / B | 本轮 / B | 减少 / B | Keil / B | 本轮 / Keil |
| --- | ---: | ---: | ---: | ---: | ---: |
| p080 TFT | 19707 | 19683 | 24 | 18071 | 108.92% |
| p091 USB MSC | 5613 | 5559 | 54 | 4401 | 126.31% |

两个工程均通过 `FINAL_ACTUAL_CALL_PRESERVATION_VERIFIED`。这是体积结果，没有完整工程相对 Keil 的运行时间成绩。TFT 继续低于体积 110% 线，MSC 仍高于 120%。

主要函数变化：`LCD_ShowChar` 573→557 B；`usb_setup` 225→198 B；`usb_read_reg` 42→32 B；`usb_write_reg` 33→28 B；`usb_in_ep1` 66→60 B；`usb_out_ep1` 57→51 B。整工程收益较小，不把下方条件密集内核的降幅推广到全部工程。

## 实现

`wide_predicate_branch.c.inc` 在发射时识别单用、分支后不再活跃的 AND/GETABIT 结果。常量掩码覆盖一个字节时，只读取该字节进行判断；单个位使用 `JB/JNB`，多位使用 `ANL A,#mask` 和零判断，全字节掩码直接判断。支持 8/16/32 位来源、C 整数提升、低组字节寄存器、高组字寄存器以及溢出槽。来源仍可供其他消费者使用。

紧邻的单用字节读取可与判断一起发射，允许跳过已删除操作留下的 MIR NOP，不跨标签、调用或其他实际操作。直接对象、栈参数、局部对象保持原有地址与一次读取；volatile 宽对象仍完整读取全部字节，然后在已取到的值上选位。多字节掩码、需要符号扩展部分的掩码、仍有其他用途的掩码结果继续使用原路径。

已有位变量读取加判断也改用直接位跳转。分支记录新增位操作数，沿用完整发射后的距离松弛；长距离仍采用反条件加 EJMP。指令效果分析补齐 `JB/JNB acc.n` 对 A 的读取。没有增加分配候选搜索层，也没有改变调用 ABI。

## 独立夹具与实板

新增 `predicate-branches.c`，包含全部 8 个位、32 位值的各字节、单字节多位掩码、volatile 字节/宽对象/局部对象、位变量、保留来源和掩码结果、符号扩展及多字节掩码。每个输入循环 16 次，64 个输入共同覆盖全部 256 种字节值；另有既有控制流组。

AI8051U-34K64、40 MHz、Timer0 /12T、UART1 115200。自动定位 STC-Link1D/COM13，父版与新版分别烧录并核对独立 Python 参考结果。

| 项目 | 父版 | 本轮 |
| --- | ---: | ---: |
| 夹具固件 / B | 1854 | 1653 |
| 条件内核 tick 汇总 | 26434 | 23533 |
| 控制组 tick 汇总 | 10147 | 10087 |
| 检查项 | 128 通过 | 128 通过 |
| 有效帧 | 630 | 623 |

取各输入多帧中位数后求和，无计时溢出。夹具体积减少 **10.84%**，条件内核耗时减少 **10.97%**；没有此夹具的 Keil 时间分母。最后保留新版夹具固件。

其他验证：

- `check-predicate-branches.py`：small/large × speed/size 四组编译汇编通过；核对实际位跳转的三字节编码和正负位移、volatile 宽读取次数、显式被调保存下的局部帧、20 个指令效果案例。
- `check-wide-branch-bounds.py`：生产松弛代码的 3108 个距离、屏障、缺失/重复目标等案例通过，包含新增位操作数。
- 两个完整工程均完成实际调用保存核对。

## 未采用的后端选择试验

上轮 `usb_read_reg` 变长的起点发生在首次编译，早于被调保存选择：定义副本传播改变了宽后端的写寄存器集合，legacy 候选需要额外写 R7，受到既有写集合约束而没有被选中。入口 PUSH/出口 POP 不是全部增量的来源。

本轮试过解除普通/宽后端候选比较中的写集合子集要求，保留原有路径、栈和调用比较，并让构建继续核对最终调用保存。试验 `core-03` 为 TFT 19720 B、MSC 5593 B：TFT 比本轮父版大 13 B，MSC 比当时保留该约束的 `core-02` 大 7 B。因此撤回这一试验，最终保留原选择规则。不能只凭被调函数更短就认为整个调用图更好。

试验补丁 `backend-choice-trial.patch`、各阶段产物和原始日志保存在研究目录；没有把未采用版本烧到板上。

## 复现

研究目录：`E:/STVAPP/MX251-mainline-20261110/predicate-branches`。最终开发核心 `core-04`，编译器 SHA-256 为 `d9c08f6d7072a573ada970390030cdf479fbd40a97181ffab92ecdab69ce8285`。公开发布工具链未更新；此快照生成后只统一了修改行的换行符，机器语义不变。

```powershell
python core/sources/compiler/src/mcs251/tests/check-predicate-branches.py --sdcc <sdcc.exe> --support <toolchain> --cc <gcc.exe> --output <新目录>
python tools/research/predicate_branches_fixture.py create --output <新工程目录>
python tools/research/predicate_branches_fixture.py capture --hex <firmware.hex> --output <新记录目录>
```

`core-04-p080/p091` 为完整工程；`board-parent/new` 为夹具构建；`capture-parent/new` 保存下载、原始 UART 和逐输入结果。
