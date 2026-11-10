# 完整函数的后端选择

2026-11-10，基于 `58136f3`，分支 `codex/complete-backend-selection`。本轮改进 MX251 对普通后端和宽后端的选择：比较已经生成的完整函数，把分支、字段地址和共享尾部计入成本。三个冻结工程分别减少 **89 / 68 / 63 B**，SDK 输入哈希一致。[证据](complete-backend-selection-evidence.json)记录完整工程、实板和编译检查。

## 机制

原选择器主要比较直线代码，或要求候选在每个共同 IR 区域的路径指标上均占优。包含条件分支、共享尾部的函数经常不能完成比较，较小的普通后端候选因此未被采用。

新增完整函数选择用于 C 控制流无环的已有合格函数。对基本块后继图做拓扑消除，结构化循环和不可约 goto 循环仍使用原选择器。两份代码都具有完整编码成本时，只有普通后端的总字节数更少、静态指令数不更多，才选择普通后端。未知成本保留宽后端；已有窄无符号取负试验的选择规则保持原样。选择不依赖工程名、函数名或器件名。

同时补齐成本信息：

- 识别 `((_Setup+0x0006)+1)` 这一类编译器生成的嵌套字段地址。解析恰好一个已声明对象及整数加减，最终偏移必须位于对象内；保留符号大小写，不接受对象地址取负、多个符号或未知表达式。
- 完整函数计价补上 Source 模式 `DJNZ`、`CPL A` 和 `PUSHW #symbol`，复用已有 CJNE/ADDC/SUBB 成本。
- 候选按完整函数选择，最终寄存器破坏集合仍由现有构建反馈按实际代码计算。ISR 保存量和调用者的变化计入完整镜像结果。

`MX251_BODY_BACKEND_CHOICE=0` 可关闭新选择策略用于研究对照；嵌套字段地址的成本识别仍在。新诊断 `mx251-body-choice` 标识使用该策略的函数。

这是体积与静态指令数启发式，并非逐路径周期证明。C CFG 无环也不意味着最终汇编无环，例如变量移位仍可生成循环。实板下面记录了实际时间变化，没有把代码变小写成每条路径都更快。

## 完整工程

沿用 small/speed、FunctionSharing、MachineIr、CalleePreservation、调用反馈与函数 GC；TFT/MSC 使用 NativeIntegerRuntime。三个工程最终调用保存核对均通过，输入文件及哈希与父版一致。

| 工程 | 父版 / B | 本轮 / B | 减少 / B | Keil / B | 本轮 / Keil |
| --- | ---: | ---: | ---: | ---: | ---: |
| p080 TFT | 19497 | 19408 | 89 | 18071 | 107.40% |
| p087 USB CDC | 4434 | 4366 | 68 | 3531 | 123.65% |
| p091 USB MSC | 5360 | 5297 | 63 | 4401 | 120.36% |

MSC 距体积 120% 线还差 **16 B**，距 110% 线还差 **456 B**。这些不是完整工程体积与时间双达标成绩。

CDC 的 `usb_clear_feature` 178→148 B、`usb_set_feature` 166→146 B、`usb_uart_settings` 64→50 B；MSC 对应前两项为 150→127、140→121 B，`scsi_request_sense` 89→75 B。两工程 `reverse2` 均为 40→34 B。ISR 保存成本分别增加 8/4 B，已经扣除在净收益中。

TFT 的 `LCD_SetWindows` 180→163 B、`LCD_DrawPoint` 55→46 B；调用者也随最终调用破坏反馈受益，`Fill_Triangel` 714→670 B。循环函数没有因此切换到新的完整函数选择策略。

## 验证

`check-body-choice.py` 覆盖 27 个地址表达式、small/large × speed/size 四组真实编译和汇编、结构化及不可约循环，以及关闭新策略的对照。`check-narrow-negate-encoding.py` 的 21 个指令成本与汇编器实际字节逐项一致。

独立夹具包含多条件请求判断、16 位字段、汇合分支、可变移位、字节序转换及跨函数事务记录。事务函数单独用普通后端编译，Python 按整数语义计算独立参考结果；不是编译器输出互相比较。64 个种子各执行 8 轮，另有既有控制组。

AI8051U-34K64、40 MHz、Timer0 /12T、UART1 115200；自动定位 STC-Link1D 与 COM13。两次烧录都通过 128 项，最后保留新版固件。

| 项目 | 父版 | 本轮 |
| --- | ---: | ---: |
| 固件 / B | 1591 | 1570 |
| 测试组 tick 汇总 | 11120 | 11141 |
| 控制组 tick 汇总 | 10087 | 10087 |
| 检查项 | 128 通过 | 128 通过 |
| 有效帧 | 647 | 640 |

每个输入取多帧中位数再汇总。测试固件体积减少 **1.32%**，测试组耗时增加 **0.19%**；单输入最大增加 **1.59%**，控制组不变。没有计时溢出。完整 TFT/USB 对 Keil 的耗时仍未测得。

## 已停止的试验

先试过机器指令层常量折叠与立即数替换：CDC 4429 B（−5），MSC 5373 B（+13），收益不足，完整撤回。其独立模型检查虽通过，也没有继续烧录。

随后试过允许 DATA 结构体包含数组和嵌套结构，使零偏移首成员转换能进入宽后端：CDC 不变，MSC 5366 B（+6）。宽访存收益被分支与调用保存抵消，亦完整撤回。

两份补丁及原始构建留在研究目录的 `constant-trial.patch`、`aggregate-layout-trial.patch`，避免重复探索。`core-03` 是中途选择策略试验，尚把未知成本归入普通后端；最终 `core-05` 已改为只有两份完整成本都已知时选择，不能将中途镜像作为本轮结果。

## 保存与复现

研究目录 `E:/STVAPP/MX251-mainline-20261110/machine-constants`，最终核心 `core-05`，编译器 SHA-256：`4cdcb0d7d7aaf96272b7c173a5c72a16b7573ed7bd14e0d7fed9097466fcff16`。最终验证后仅整理源码换行、补充注释与测试。公开工具链未更新。

`core-05-p080/p087/p091` 保存完整镜像与 receipt；`board-parent/new` 为测试固件；`capture-parent/new` 保存烧录日志、原始 UART 和逐输入结果。`run.py`、`hardware.py` 保存构建入口。

```powershell
python core/sources/compiler/src/mcs251/tests/check-body-choice.py --sdcc <sdcc.exe> --support <toolchain> --gcc <gcc.exe> --output <新目录>
python core/sources/compiler/src/mcs251/tests/check-narrow-negate-encoding.py --host-cc <gcc.exe> --assembler <sdas251.exe> --output-dir <新目录>
python tools/research/body_choice_fixture.py create --output <新工程目录>
python tools/research/body_choice_fixture.py capture --hex <firmware.hex> --output <新记录目录>
```
