# 控制流中的地址事实接入调用效果

2026-11-11，基线 `fdbf6d8`，实现 `d9557d4`，分支 `codex/cfg-address-effects`。

分支共享和轮询循环会让地址经过汇合标签。此前调用分析在标签、跳转处清空地址常量，即使各路径的地址都在同一外设区域，后续间接写入也会让整个函数变成“可能改写全部寄存器”。本版保留控制流上各路径一致的地址字节，让已有调用保存和中断保存精简使用实际的寄存器写集合。

这是 MX251 构建期调用反馈的一部分。`sdcc.exe` 沿用上一版的相同二进制；新核心更新了调用效果分析和构建记录。正常构建入口默认使用新分析，SDK 源码与运行库未修改。

2026-11-11：统一核心 **75/75 构建成功，70 项体积 ≤Keil 120%，其中 56 项 ≤110%**。4 项缩小、71 项相同、0 项变大，净减少 **108 B**；合计 **203230 / 192051 B（105.82%）**。新过 120% 线：无；新过 110% 线：无。完整工程运行时间未测。

| 工程 | 父版/B | 本版/B | 减少/B | Keil/B | 本版/Keil |
| --- | ---: | ---: | ---: | ---: | ---: |
| p051 | 661 | 649 | 12 | 608 | 106.74% |
| p060 | 1524 | 1492 | 32 | 1473 | 101.29% |
| p114 | 3481 | 3449 | 32 | 3250 | 106.12% |
| p129 | 3481 | 3449 | 32 | 3250 | 106.12% |

## 实现

新增 `core/build/qualified/CallAddressFacts.ps1`。分析建立汇编指令 CFG，以物理字节表示 R0–R31 和 DPX，传播 MOV、零/符号扩展及按字节可确定的位运算结果。汇合处只保留所有进入路径都相同的字节，循环迭代到事实不再变化。

例如 p051 的三条路径分别选择 `0x7efd5b`、`0x7efd5f`、`0x7efd53`。低字节在汇合后不同，上面的字节仍确定，因而写入范围可描述为 `0x7efd00..0x7efdff`。这足以确认该写入不涉及核心寄存器，同时保留写入操作本身。

分析把未知字节展开为地址区间，并将访问宽度、常量位移计入末端。普通调用清除地址事实；部分寄存器覆盖按物理别名更新。没有地址依据的动态指针继续使用原来的调用效果结果。

仅在原分析遇到未知间接写入时运行 CFG 分析。`MX251_CFG_ADDRESS_EFFECTS=0` 可关闭该补充分析。构建 receipt 新增 `callClobberFeedback.addressFactsSha256`，绑定具体实现文件。

## 实际收益

已有成品扫描确认 5 个函数获得完整地址写入描述：p019 `MCLK_Sel`、p051 `UpdatePwm`、p060 `Wait`、p114/p129 `ReadStatusLCD`。p019 完整镜像持平；分析覆盖扩大并不自动等于减少代码。

| 工程 | 单元 | 函数 | 父版/B | 本版/B | 减少/B |
| --- | --- | --- | ---: | ---: | ---: |
| p051 | u0 | _main | 113 | 109 | 4 |
| p051 | u0 | _timer0 | 180 | 172 | 8 |
| p060 | u0 | _WriteNbyte | 91 | 75 | 16 |
| p060 | u0 | _ReadNbyte | 115 | 99 | 16 |
| p114 | u0 | _DMA_Config | 89 | 73 | 16 |
| p114 | u0 | _WriteDataLCD | 65 | 57 | 8 |
| p114 | u0 | _WriteCommandLCD | 65 | 57 | 8 |
| p129 | u0 | _DMA_Config | 89 | 73 | 16 |
| p129 | u0 | _WriteDataLCD | 65 | 57 | 8 |
| p129 | u0 | _WriteCommandLCD | 65 | 57 | 8 |

函数数字来自最终链接 HEX。完整工程统计包含调用者寄存器分配、保存恢复和布局的变化。

## 检查与实板

- 22 项 CFG 地址检查：分支汇合、循环回边、动态低半部、字节/字寄存器重叠、DPTR/DPXL 别名、调用、位移、24 位末端及关闭对照。
- 31 项已有对象、跨单元调用闭包和寄存器别名检查通过。
- AI8051U 40 MHz，STC-Link1D 自动发现，COM13；父版和新版各 128 个输入通过独立 Python 结果检查。
- 真实 Timer0/1 中断调用三路地址选择函数及轮询函数，核对四组经典寄存器状态、R0–R31、DPX、PSW/PSW1、栈和 RAM 写入结果。

| 实板代码 | 父版/B | 本版/B |
| --- | ---: | ---: |
| 三路写入调用 ISR | 76 | 72 |
| 三路写入与轮询调用 ISR | 80 | 76 |
| 完整测试固件 | 2331 | 2323 |

测试使用停止的计时器和软件中断标志；采集记录里的 tick 不用于运行时间结论。完整 SDK 工程及其与 Keil 的运行时间对比仍未测。

## 产物与复现

推荐核心：`E:/STVAPP/MX251-mainline-20261111/address-effects/core-02`。

- `Build-Optimized-02.ps1`：同目录构建入口。
- `comparison.json/.md/.html`：75 项统一比较，Keil/STC 复用源输入一致的冻结结果。
- `qualification.json`、`attribution.json`：源码、工具、HEX、检查及函数归因。
- `checks-01/`、`object-checks/`：分析测试。
- `board-parent/`、`board-candidate/`、`hardware-parent/`、`hardware-candidate/`：构建与实板记录。
- [结构化证据](cfg-address-effects-evidence.json)。

```powershell
python -X utf8 tools/research/cfg_address_effects_fixture.py create --output <新夹具目录>
pwsh -File core/sources/compiler/src/mcs251/tests/check-cfg-address-effects.ps1 -OutputDirectory <新检查目录>
python -X utf8 tools/research/unified_sdk_matrix.py --config E:/STVAPP/MX251-mainline-20261111/address-effects/core-02.json --suites mx251 --jobs 8
```

夹具使用正常构建链生成后，由 `cfg_address_effects_fixture.py capture --hex <固件> --output <新记录目录>` 自动寻找下载器和串口。

当前超出 120% 体积线：

| 工程 | MX251/B | Keil/B | 到 120% 还需减少/B |
| --- | ---: | ---: | ---: |
| p045 | 1052 | 832 | 54 |
| p047 | 194 | 160 | 2 |
| p049 | 1210 | 1002 | 8 |
| p108 | 7754 | 1434 | 6034 |
| p149 | 509 | 423 | 2 |
