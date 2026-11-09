# 宽后端的普通变参调用

2026-11-09，从 `61a5127` 接续，分支 `codex/variadic-call-lowering`。补齐 MX251 宽后端调用普通 ABI2 变参函数的能力，并接通已有格式化运行库裁剪。SDK 源码和输入配置未改。指纹见 [variadic-call-lowering-evidence.json](variadic-call-lowering-evidence.json)。

## 实现

现有前端和旧后端对变参调用采用全部参数压栈：固定参数也不占用普通首参寄存器，返回值仍使用原 ABI2 规则。宽后端此前直接拒绝变参调用，导致含有 `printf`、`sprintf` 或自定义变参接口的整个函数退回旧后端。

本轮复用已有的物理压栈、调用保存、栈帧、结果接收和符号地址传参实现：

- 固定参数按原型确认宽度，可变参数沿用前端完成提升后的实际操作数类型。C 模式按标准提升；SDCC 模式保留显式字节转换已有的一字节扩展。
- 每个参数保持原 `IPUSH` 次序和物理宽度；volatile 实参继续在原位置取样，随后运输取样值。符号指针直接压入三字节重定位。
- 容纳最多 16 个总参数全部在栈上的情况，同步调整调用节点、导入暂存和权重重建数组。原总参数上限保持 16。
- 支持整数、32 位浮点和三字节平坦指针的既有返回规则，普通及原生 ABI4 调用者均可调用 ABI2 变参函数。变参函数本体仍由旧后端编译。

编译器不按 `sprintf` 或 SDK 函数名称选择优化。没有新增编译开关，公开 ABI 和标准库实现不变。

## 格式化运行库衔接

第一版 `core-01` 的 `Test_Read` 已缩小，但构建工具未识别新后端的参数输出，TFT 因而链接完整格式化库，达到 26862 B。最终版完成两处衔接：

1. 宽后端为每个变参栈参数发出已有的参数区域标记，范围包含实际压栈指令，不包含调用者保存。
2. `FormatRuntime.ps1` 识别 `#((symbol+0)>>16)` / `PUSHW #(symbol+0)`，并按函数所有者查找可位于函数末尾的 ABI 声明。仍从实际指令统计每个参数的字节数，与格式转换逐项匹配。

已有固定整数格式裁剪恢复生效，没有新增格式转换或改写 SDK 格式串。运行库选择仍有自己的完整镜像检查。

## 完整工程结果

各工程源码哈希、项目配置和构建选项与父版一致，继续显式启用 `CalleePreservation`。四项调用反馈均为 `FINAL_ACTUAL_CALL_PRESERVATION_VERIFIED`。

| 工程 | 父版 / B | 本轮 / B | 减少 / B | Keil / B | 本轮 / Keil |
| --- | ---: | ---: | ---: | ---: | ---: |
| p080 TFT | 20563 | 20481 | 82 | 18071 | 113.34% |
| p087 USB CDC | 4581 | 4581 | 0 | 3531 | 129.74% |
| p091 USB MSC | 5651 | 5651 | 0 | 4401 | 128.40% |
| p095 双 CDC | 6416 | 6416 | 0 | 4830 | 132.84% |

TFT `Test_Read` 从 666 降到 580 B；调用反馈使 `LCD_DrawFillRectangle` 从 51 变为 55 B，整工程净减 82 B。TFT 格式运行库状态为 `APPLIED_CLOSED_FIXED_INTEGER_FORMAT_RUNTIME`。

最佳候选台账仍为 **40 个体积达到 120%，其中 18 个达到 110%**。本轮没有全 SDK 重跑，也没有新增完整工程的体积、时间双达线记录。

## 实板与编译检查

独立用例将新后端调用者与旧后端变参接收函数连接，并使用标准库 `sprintf`。覆盖混合整数宽度、负数默认提升、三字节 CODE/栈对象地址、32 位浮点实参与返回、指针返回、volatile 多次取样、取址局部变量被调用者修改、16 个总参数、没有额外参数的调用，以及实际字符串输出。主机单独计算期望散列。

AI8051U-34K64，40 MHz，Timer0 /12T，UART1 115200。STC-Link1D 自动找到 COM13。

| 项目 | 父版 | 本轮 |
| --- | ---: | ---: |
| 测试固件 / B | 13316 | 11221 |
| 变参及格式化用例，64 输入的中位 tick 汇总 | 66944 | 59387 |
| 控制组，同口径 tick | 10162 | 10162 |
| 正确性 | 128 项、581 帧通过 | 128 项、593 帧通过 |

测试固件减少 **2095 B（15.73%）**，综合用例耗时减少 **11.29%**，无计时溢出。这是相对父版 MX251 的结果，不是 Keil 或完整 TFT 工程计时。该测试固件使用完整标准格式化库，因此计时包括相同的库内部工作。

检查记录：

- `check-variadic-calls.py`：small/large × speed/size 四组通过，分别检查 SDCC/C 标准模式的提升宽度、ABI4 调用者、16 参数、返回运输及既有容量/类型边界。
- `check_variadic_format.ps1`：旧/新符号地址形式和末尾 ABI 声明通过；ABI、参数宽度、符号及偏移不匹配时不接受该格式裁剪。
- 最终夹具把取样全局变量显式声明为 DATA，使两种模型检查相同的存储空间。最终父/新镜像分别与已下载的镜像 SHA-256 相同，没有重复烧录。

复现入口：

```powershell
python core/sources/compiler/src/mcs251/tests/check-variadic-calls.py --sdcc <sdcc.exe> --support <toolchain目录> --output <新检查目录>
pwsh -File tools/research/check_variadic_format.ps1 -Output <新格式检查目录>
python tools/research/variadic_call_fixture.py create --output <新工程目录>
python tools/research/variadic_call_fixture.py capture --hex <firmware.hex> --output <新记录目录>
```

研究目录：`E:/STVAPP/MX251-mainline-20261109/variadic-calls`。完整工程在 `core-02-p080/p087/p091/p095`；最终实板构建在 `hardware-parent-03`、`hardware-new-04`，原始串口记录在 `capture-parent`、`capture-new`。检查在 `check-03`、`check-format-02`。开发核心 `core-02` 的编译器 SHA-256 为 `2febdd9fbd58dfe9e9196b088dac9e2c46ee89f35f53c0ccb6c2dcf5a32c742c`，源码以本分支为准。

本轮完成调用者迁移；变参接收函数和完整格式库继续使用已有实现。后续扩大收益应依据剩余函数的实际体积组成选择目标，避免继续追逐单个调用点的小差异。
