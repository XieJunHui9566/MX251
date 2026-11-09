# 机器级调用约束与调用间栈副本：2026-11-09

本次把调用约束提前到寄存器分配之前，并为连续调用中的活值生成共享栈副本。代码、编译测试和 AI8051U 实板验证已完成；四个完整工程的代码体积与父版持平。机制测试的缩小不能作为 SDK 或 Keil 对比收益。

## 实现

- `wide_call_contract` 记录被调函数写集合、实参搬运位置、临时寄存器和返回值位置。分配成本和调用发射共用这些信息，覆盖普通 ABI2、原生 ABI4 及内部运行库约定。
- 删除原先先发射一次、从汇编学习可省保存集合、再整函数重发射的过程。已知调用效果直接用于保存选择，发射器仍核对实际实参和返回值搬运的写集合。
- `wide_call_plan.c.inc` 对同一基本块内跨多次调用的活值建立私有栈副本。第一次需要保存时写入，在真正使用或离开基本块前恢复；重新定义使旧副本失效。
- 副本槽不与 C 可取址对象或已有溢出槽重叠。跨分支、合流和循环回边保持原有寄存器状态；当前不跨基本块缓存。
- 每个值按实际搬运指令数筛选，再用完整函数的编码字节、加权指令及已有路径成本决定是否采用。额外栈帧、位移变化和序言/尾声均计入。已有包含旋转合成的函数暂不生成此候选。
- 多节点融合不能跳过待执行的副本读写。缓存只代表调用者保存了值，不宣称被调函数保留该寄存器。

这仍是现有宽后端上的机器调用计划，并未完成整个编译器的统一 Machine IR 或全局活跃区间拆分；没有修改 SDK、扩大 ABI 参数寄存器或调节分配权重。

## 结果

父版为 `7174e60`。编译器 SHA-256、源文件及构建记录见 [machine-call-plan-evidence.json](machine-call-plan-evidence.json)。

机制测试包含连续调用、分支、循环和变量重新赋值，分别运行 small/large、speed/size 和两种公开 ABI，共 8 组：

| 测试集 | 父版 CSEG | 当前 CSEG | 减少 |
|---|---:|---:|---:|
| ABI4 | 698 B | 566 B | 132 B，18.91% |
| ABI2 | 748 B | 640 B | 108 B，14.44% |

其中循环示例的候选未通过路径成本比较，保持原实现。不能把所有示例都称为获得收益。

正常 CMake 入口、相同源码、small/speed、默认调用反馈的完整工程结果：

| 工程 | 父版 | 当前 | 减少 |
|---|---:|---:|---:|
| p080 TFT | 23161 B | 23161 B | 0 |
| p087 USB CDC | 4769 B | 4769 B | 0 |
| p091 USB MSC U盘 | 6312 B | 6312 B | 0 |
| p095 USB 双 CDC | 6615 B | 6615 B | 0 |

TFT 的若干候选虽然减少指令，却增加代码体积，例如 `LCD_DrawRectangle` 从 140 B 变成 156 B、`_draw_circle_8` 从 237 B 变成 245 B，因此未被采用。p091 的 `flash_writesector` 候选也有同样情况；另外两个 USB 工程没有生成有用的候选。此处的障碍是副本访问和搬运成本，不能单靠增加搜索次数解决。

另试验过在后端选择中并入被调函数写集合，TFT 完整工程增加 58 B，已撤回。该试验的 `core-02` 和日志只用于记录否定结果，不是当前交付编译器。补丁保存在本机 `E:/STVAPP/MX251-mainline-20261109/machine-call/backend-call-union-rejected.patch`。

## 验证与复现

- `check-machine-call-plan.py`：8 组编译/汇编对比，检查副本槽范围和槽间不重叠。
- `check-wide-calls.py`：small/large 下各 33 个原生调用函数通过，含参数溢出和尾调用。
- `check-wide-bit-call-results.py`：8 组通过，覆盖 CY 返回捕获、栈清理、结果溢出及未启用宽后端时对象不变。
- 现有成本模型回归通过，输出 `modelQualified: true`。
- 早期原型的普通调用测试另通过 18 组，证据中保留对应编译器哈希，未冒充当前二进制的测试。

实板使用 AI8051U-34K64、40 MHz、UART1 P3.0/P3.1、115200 8N1。`machine-call-clobber.asm` 每次调用显式覆盖 R0..R31，测试调用者必须实际保存其活值。`machine-call-hardware.c` 同时调用 ABI4 与 ABI2 的五类函数。主机独立计算期望值，575 帧全部通过，覆盖 16 组边界输入、每组 10 个结果。STC-Link1D 和串口通过本次 USB 枚举选取，保留本次固件。

硬件工程把 `machine-call-hardware.c`、`machine-call-plan.c`、`machine-call-ordinary.c`、`machine-call-clobber.asm` 和 `machine-call-plan.h` 全部列入 `target_sources`；`mx251.json` 使用 `explicitAbi: true`。串口原始记录可用 `check-machine-call-hardware.py <uart.bin>` 复查。

当前开发核心：`E:/STVAPP/MX251-mainline-20261109/machine-call/core-final`。完整工程、测试输出、硬件工程及原始串口记录均在其上级目录。宿主编译增量目录仍为 `E:/STVAPP/MX251-mainline-20261109/copy-affinity/host/build`。活动工作树为 `C:/Users/jav/.codex/worktrees/stclink1d-feasibility/MX251`，分支 `codex/machine-call-plan`。原始 `E:/STVAPP/MX251` 中已有的三个构建脚本修改保持原样。

## 后续主线

下一步应让机器调用计划直接使用栈副本设置实参，消去“恢复到原寄存器，再搬到实参位置”的额外搬运，并把这些位置关系传到分配器。先以完整 TFT/USB 工程判断收益，不增加分配权重试探或重复验证轮次。需要进一步重构时沿这个共性成本推进。

未测量本次与 Keil C251 同条件的运行时间；SDK 120% 和最终体积/运行时间均不超过 Keil 110% 的目标仍未完成。硬件下载已获持续授权，后续枚举实际 USB 位置后直接使用，无需再次确认或恢复旧固件。
