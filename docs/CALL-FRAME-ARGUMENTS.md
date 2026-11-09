# 栈副本直接传参：2026-11-09

基于 `55c59b9`，让机器调用计划中的私有栈副本直接成为实参来源，省去恢复到原寄存器再传参的步骤。完整 TFT 工程从 23161 B 降到 23153 B，减少 8 B；三个 USB 工程持平。当前实际工程收益仍小，不据此宣布 SDK 或 Keil 对比目标完成。

## 实现与适用范围

`wide_call_plan.c.inc` 在值已经有有效栈副本、寄存器内容待恢复且本次用途为 CALL 时，记录 `callArguments`。跨基本块仍恢复后继需要的寄存器；重新定义使旧副本失效。若候选被整函数成本比较淘汰，保持原实现。

发射器通过 `wsource_location` 查询操作数的读取位置。该视图仅在发射当前 CALL 时生效，覆盖参数装载、源寄存器依赖排序和栈实参压入；结果写入继续使用原分配位置。参数和结果即使对应同一个虚拟值，也不会误把返回值写进旧副本槽。

普通 ABI2 的栈值直接送到 B:A / DPL / DPH，省去 DR16 快照中转；适用于实际分配的 spill 和调用计划副本，覆盖 1/2/3/4 个物理参数字节。三字节指针仍按 ABI 传递；字节栈实参按当前 SPX 深度读取。原生 ABI4 也可直接从副本装入参数寄存器，字节参数不再经 A 搬一次。SDK 和 ABI 定义没有变化。

## 收益与限制

正常 CMake 构建、small/speed、默认反馈、输入源码哈希相同：

| 工程 | 父版 | 当前 | 减少 |
|---|---:|---:|---:|
| p080 TFT | 23161 B | 23153 B | 8 B，0.0345% |
| p087 USB CDC | 4769 B | 4769 B | 0 |
| p091 USB MSC U盘 | 6312 B | 6312 B | 0 |
| p095 USB 双 CDC | 6615 B | 6615 B | 0 |

TFT 的收益来自两处普通栈实参中转消除。现有四个完整工程仍未采用调用副本传参候选：新增槽访问的完整编码成本尚未低于原保存方式。例如 `LCD_DrawRectangle` 候选为 140 → 164 B，未采用。下一步不能把“再增加一些候选”当作解决办法，应结合实际指令长度处理分配位置与短 PUSH/POP 的成本差异。

专项测试把重复传参和会覆盖全部通用寄存器的其他调用交错，覆盖字节、字、双字、原生 ABI 和三字节指针五种路径。small/large × speed/size 共 4 组均为 1198 → 1143 B，减少 55 B（4.59%），每组有 50 次实参直接读取副本。这是机制测试，不代表完整工程收益。

## 验证

- `check-call-frame-arguments.py`：上述 4 组通过。
- `check-wide-ordinary-calls.py`：18 组通过，包括递归、栈参数、地址别名、浮点位运输和未启用普通宽后端时的对象隔离。
- `check-wide-calls.py`：small/large 各 33 个原生调用函数通过。栈读取识别新增合法 R0..R15 直接装载形式，原来的栈深度平衡和调用前实际读取检查保留。
- AI8051U-34K64，40 MHz，UART1 P3.0/P3.1，115200 8N1。通过 USB 枚举定位 STC-Link1D 及 UART 后下载，597 帧全部通过，覆盖 16 组输入、每组 10 个值。各参数提供函数调用汇编 `barrier` 覆盖 R0..R31；主机独立验证返回值及每次调用累积的有序记录。保留本次固件。

硬件源文件为 `call-frame-hardware.c`、`call-frame-arguments.c`、`call-frame-providers.c`、`machine-call-clobber.asm`；两个头文件也列入 CMake `target_sources`，`explicitAbi: true`。串口记录可用 `check-call-frame-hardware.py <uart.bin>` 复查。

编译器、固件及源文件指纹见 [call-frame-arguments-evidence.json](call-frame-arguments-evidence.json)。完整产物位于 `E:/STVAPP/MX251-mainline-20261109/call-arguments/`，开发核心为其中 `core/`。活动工作树为 `C:/Users/jav/.codex/worktrees/stclink1d-feasibility/MX251`，分支 `codex/call-frame-arguments`。原始工程已有修改保持原样。

未测量本次相对 Keil 的运行时间。局部路径和加权指令成本用于候选选择，不等于实板运行时间。
